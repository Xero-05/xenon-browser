// Production CEF/MCP; disposable loopback fixtures only. Timeout recovery itself
// is deterministic in screenshot_guard_tests, without production fault hooks.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';

const root=resolve(import.meta.dirname,'..');
const build=resolve(root,process.env.XENON_TEST_BUILD_DIR??'build');
const run=`screenshot-integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile=resolve(root,'.cache',run),pipeName=`xenon-test-${run}`,pipe=`\\\\.\\pipe\\${pipeName}`;
const dll=resolve(build,'app/Release/Xenon.dll'),config=resolve(profile,'client.json');
const hash=async()=>createHash('sha256').update(await readFile(dll)).digest('hex');
const runFile=promisify(execFile),sleep=ms=>new Promise(r=>setTimeout(r,ms));
const results=[],ready=new Set();
let browser,client,server,base,cross,worker,initialHash;
async function until(fn,timeout=20000){
  const end=Date.now()+timeout;
  while(Date.now()<end){const value=await fn();if(value)return value;await sleep(150);}
  throw new Error('Fixture condition timed out');
}
const alive=()=>new Promise(resolve=>{
  const socket=net.connect(pipe);
  socket.once('connect',()=>{socket.destroy();resolve(true);});
  socket.once('error',()=>{socket.destroy();resolve(false);});
});
const raw=(name,args={})=>client.callTool({name:`xenon_${name}`,arguments:args});
async function tool(name,args={}){
  const value=await raw(name,args);
  assert(!value.isError,`${name}: ${JSON.stringify(value.structuredContent)}`);
  return value.structuredContent;
}
const scope=tab=>({agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,tabId:tab.tabId});
async function page(mode){
  const key=randomUUID();
  const tab=await tool('tab_create',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,url:`${base}/page?mode=${mode}&key=${key}`});
  await until(()=>ready.has(key));
  await until(async()=>{
    const value=await raw('observe',{...scope(tab),maxNodes:100});
    return !value.isError&&!value.structuredContent.loading&&value.structuredContent.nodes?.some(n=>n.name==='Screenshot fixture ready');
  });
  return tab;
}
async function screenshot(tab){
  return until(async()=>{
    const reply=await raw('screenshot',scope(tab));
    if(reply.isError&&['privacy_guard_initializing','page_changed','screenshot_unverified'].includes(reply.structuredContent?.error?.code))return false;
    return reply;
  },30000);
}
function expectImage(reply){
  assert(!reply.isError,JSON.stringify(reply.structuredContent));
  assert(reply.content.some(c=>c.type==='image'&&c.mimeType==='image/png'&&c.data.length>100));
  assert(reply.structuredContent.observationId);
  assert(reply.structuredContent.imageWidth>0&&reply.structuredContent.imageHeight>0);
}
function expectProtected(reply){
  assert(reply.isError,'Credential document must not return pixels');
  assert.equal(reply.structuredContent.error.code,'screenshot_protected');
  assert.equal(reply.structuredContent.error.reason,'credential_fields');
  assert.equal(reply.structuredContent.error.retryable,false);
  assert(!reply.content.some(c=>c.type==='image'));
}
async function check(name,fn){
  try{const detail=await fn();results.push({name,passed:true,...(detail?{detail}:{})});console.log(`PASS ${name}`);}
  catch(error){results.push({name,passed:false,error:error.message});console.error(`FAIL ${name}: ${error.message}`);}
}
try{
  await mkdir(profile,{recursive:true});
  await writeFile(resolve(profile,'SYNTHETIC_TEST_PROFILE'),'XENON_SYNTHETIC_SCREENSHOT_FIXTURE\n');
  await writeFile(resolve(profile,'ui-settings.json'),JSON.stringify({version:1,introductionCompleted:true,automaticUpdateChecks:false}));
  const identity={clientId:`screenshot_${randomBytes(8).toString('hex')}`,token:randomBytes(32).toString('hex'),pipe};
  await writeFile(resolve(profile,'broker-state.json'),JSON.stringify({version:1,
    clients:[{id:identity.clientId,name:'Synthetic screenshot client',tokenHash:createHash('sha256').update(identity.token).digest('hex')}],
    workspaces:[{id:'native-default',clients:[identity.clientId]}],accountGrants:[],operations:[]}));
  await writePrivateConfig(config,identity);
  server=http.createServer((request,response)=>{
    const url=new URL(request.url,'http://127.0.0.1');
    response.setHeader('Cache-Control','no-store');
    if(url.pathname==='/ready'){ready.add(url.searchParams.get('key'));response.end('ok');return;}
    response.setHeader('Content-Type','text/html; charset=utf-8');
    const mode=url.searchParams.get('mode')??'safe';
    if(url.pathname==='/frame'){
      const depth=Number(url.searchParams.get('depth')??0);
      const next=request.headers.host.startsWith('localhost')?base:cross;
      response.end(`<p>Frame content</p>${mode==='password'?'<input type="password" aria-label="Synthetic empty password">':''}${depth>0?`<iframe src="${next}/frame?depth=${depth-1}"></iframe>`:''}`);
      return;
    }
    let content='';
    if(mode==='password')content='<input type="password" aria-label="Synthetic empty password">';
    if(mode==='shadow')content='<div id="shadow"></div><script>document.querySelector("#shadow").attachShadow({mode:"open"}).innerHTML=\'<input autocomplete="one-time-code" aria-label="Synthetic empty code">\'</script>';
    if(mode==='nested'||mode.startsWith('same-')||mode.startsWith('cross-')){
      const frameBase=mode.startsWith('same-')?base:cross;
      const depth=mode==='nested'?3:0;
      content=`<iframe id="child" src="${frameBase}/frame?depth=${depth}&mode=${mode.endsWith('password')?'password':'safe'}"></iframe>
        <button id="replace">Replace frame</button><script>
        let replacements=0;
        document.querySelector("#replace").onclick=()=>{
          document.querySelector("#state").textContent="Replacing frame";
          const old=document.querySelector("#child"),fresh=document.createElement("iframe");
          fresh.id="child";fresh.src="${cross}/frame?depth=${depth}&mode=safe";
          fresh.onload=()=>{document.querySelector("#state").textContent="Safe replacement loaded "+(++replacements)};
          old.replaceWith(fresh);
        };</script>`;
    }
    response.end(`<!doctype html><style>body{background:white;color:black;font:18px sans-serif}iframe{width:400px;height:100px}</style>
      <h1>Screenshot fixture ready</h1><p id="state">Synthetic page</p>${content}
      <script>addEventListener("load",()=>fetch("/ready?key="+encodeURIComponent(new URL(location.href).searchParams.get("key"))))</script>`);
  });
  await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
  base=`http://127.0.0.1:${server.address().port}`;cross=`http://localhost:${server.address().port}`;
  await check('Readiness keys are never reflected into fixture HTML',async()=>{
    const key='</script><script>globalThis.fixtureInjection=true</script>';
    for(const mode of ['safe','cross-safe','nested']){
      const reply=await fetch(base+'/page?mode='+mode+'&key='+encodeURIComponent(key));
      assert.equal(reply.status,200);
      const html=await reply.text();
      assert(!html.includes(key),'Request keys must not become executable fixture HTML');
      assert(!html.includes('fixtureInjection'),'No part of the supplied script may be reflected');
    }
  });
  initialHash=await hash();
  browser=spawn(resolve(build,'app/Release/Xenon.exe'),[`--user-data-dir=${profile}`,`--broker-pipe=${pipeName}`],{windowsHide:true,stdio:'ignore'});
  let launchError;browser.once('error',error=>{launchError=error;});
  await until(async()=>{if(launchError)throw launchError;if(browser.exitCode!==null)throw new Error('Fixture browser exited');return alive();},45000);
  const transport=new StdioClientTransport({command:process.execPath,args:[resolve(root,'adapter/dist/src/cli.js'),'serve','--config',config],stderr:'pipe'});
  client=new Client({name:'Xenon screenshot regression',version:'1'},{versionNegotiation:{mode:{pin:'2026-07-28'}}});
  await client.connect(transport);
  worker=await tool('worker_create',{name:'Screenshot fixture worker',workspaceId:'native-default'});
  for(const mode of ['safe','same-safe','cross-safe'])
    await check(`Safe ${mode} page returns PNG`,async()=>expectImage(await screenshot(await page(mode))));
  for(const mode of ['password','shadow','same-password','cross-password'])
    await check(`${mode} credentials withhold every image`,async()=>expectProtected(await screenshot(await page(mode))));
  await check('Replacing a credential frame with a verified safe frame restores capture',async()=>{
    const tab=await page('cross-password');
    expectProtected(await screenshot(tab));
    const obs=await tool('observe',{...scope(tab),maxNodes:100});
    const button=obs.nodes.find(n=>n.name==='Replace frame'&&n.ref);
    assert(button,'Replacement control must be visible');
    const status=await tool('control_status',scope(tab));
    await tool('interact',{...scope(tab),ownershipGeneration:status.ownershipGeneration,observationId:obs.observationId,
      operationId:randomUUID(),action:'click',elementRef:button.ref});
    await until(async()=>{const reply=await raw('observe',{...scope(tab),maxNodes:100});return !reply.isError&&reply.structuredContent.nodes?.some(n=>n.name==='Safe replacement loaded 1');});
    expectImage(await screenshot(tab));
  });
  await check('Repeated nested cross-origin frame replacement stays capturable',async()=>{
    const tab=await page('nested'),times=[];
    expectImage(await screenshot(tab));
    for(let iteration=1;iteration<=24;++iteration){
      const obs=await tool('observe',{...scope(tab),maxNodes:100});
      const button=obs.nodes.find(n=>n.name==='Replace frame'&&n.ref);
      assert(button,'Replacement button must remain visible');
      const status=await tool('control_status',scope(tab));
      await tool('interact',{...scope(tab),ownershipGeneration:status.ownershipGeneration,observationId:obs.observationId,
        operationId:randomUUID(),action:'click',elementRef:button.ref});
      await until(async()=>{const reply=await raw('observe',{...scope(tab),maxNodes:100});return !reply.isError&&reply.structuredContent.nodes?.some(n=>n.name===`Safe replacement loaded ${iteration}`);});
      const start=Date.now();
      const reply=await raw('screenshot',scope(tab));
      times.push(Date.now()-start);
      expectImage(reply);
      assert(times.at(-1)<2500,'A settled synthetic page must not wait for the 10-second protocol timeout');
    }
    return {iterations:times.length,maxCaptureMs:Math.max(...times),captureMs:times};
  });
}finally{
  if(client)await client.close().catch(()=>{});
  if(browser?.pid&&browser.exitCode===null)await runFile('taskkill.exe',['/PID',String(browser.pid),'/T','/F'],{windowsHide:true}).catch(()=>{});
  server?.closeAllConnections();server?.close();
  const finalHash=await hash().catch(()=>undefined);
  const passed=results.length===10&&results.every(r=>r.passed)&&initialHash===finalHash;
  const report={run,binary:resolve(build,'app/Release/Xenon.exe'),applicationDllSha256:initialHash,applicationDllSha256AtEnd:finalHash,
    coverage:'Production CEF/MCP synthetic fixtures. Safe and credential pages, same-origin/cross-origin frames, shadow root, frame replacement. Protocol timeout injection is unit coverage, not live browser coverage. No personal profiles or physical input.',results,passed};
  await mkdir(resolve(root,'out'),{recursive:true});
  await writeFile(resolve(root,'out',`${run}.json`),JSON.stringify(report,null,2));
  console.log(`Report: out/${run}.json`);
  if(!passed)process.exitCode=1;
}
