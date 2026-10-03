// Production browser + real MCP with disposable state. Perform the announced
// native UI steps in this fixture's window only. The harness never drives Win32
// controls or adds an MCP configuration/close bypass.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';
const root=resolve(import.meta.dirname,'..'),run='shell-tabs-'+randomUUID();
const profile=resolve(root,'.cache',run),pipeName='xenon-'+run,pipe=`\\\\.\\pipe\\${pipeName}`;
const binary=resolve(root,'build/app/Release/Xenon.exe'),dll=resolve(root,'build/app/Release/Xenon.dll');
const configPath=resolve(profile,'client-config.json'),results=[],clients=[],telemetry=new Map();
const hash=async()=>createHash('sha256').update(await readFile(dll)).digest('hex');
const sleep=ms=>new Promise(r=>setTimeout(r,ms)),runFile=promisify(execFile);
let browser,server,base,applicationDllSha256;
const alive=()=>new Promise(r=>{const socket=net.connect(pipe);socket.once('connect',()=>{socket.destroy();r(true);});socket.once('error',()=>r(false));});
async function until(fn,timeout=30000){const end=Date.now()+timeout;while(Date.now()<end){const result=await fn();if(result)return result;await sleep(100);}throw Error('Shell fixture step timed out');}
async function tool(client,name,args={}){const r=await client.callTool({name:'xenon_'+name,arguments:args});assert(!r.isError,JSON.stringify(r.structuredContent));return r.structuredContent;}
async function blank(client,scope,tab){const observed=await until(async()=>{try{const value=await tool(client,'observe',{...scope,tabId:tab.tabId,maxNodes:30});return !value.loading?value:false;}catch{return false;}});assert.equal(observed.url,'about:blank');}
async function check(name,action){try{const details=await action();results.push({name,passed:true,details});console.log('PASS '+name);}catch(error){results.push({name,passed:false,error:error.message});throw error;}}
async function ready(action){const value={run,pid:browser.pid,profile,action};await writeFile(resolve(root,'.cache/current-shell-tabs.json'),JSON.stringify(value,null,2));console.log('READY '+JSON.stringify(value));}
async function launch(){
  browser=spawn(binary,[`--user-data-dir=${profile}`,`--broker-pipe=${pipeName}`],{windowsHide:true,stdio:'ignore'});
  await until(alive,45000);
  const client=new Client({name:'Synthetic shell tabs regression',version:'1'});
  await client.connect(new StdioClientTransport({command:process.execPath,args:[resolve(root,'adapter/dist/src/cli.js'),'serve','--config',configPath],stderr:'pipe'}));clients.push(client);return client;
}
async function exitNormally(client){
  await ready('Use Menu > Exit Xenon in this fixture window.');
  await until(()=>browser.exitCode!==null||browser.signalCode!==null,300000);
  assert.equal(browser.exitCode,0);assert.equal(browser.signalCode,null);await until(async()=>!(await alive()));await client.close();browser=undefined;
}
try{
  assert.equal(await alive(),false);await mkdir(profile,{recursive:true});
  await writeFile(resolve(profile,'SYNTHETIC_TEST_PROFILE'),'XENON_SYNTHETIC_SHELL_TABS_FIXTURE\n');
  await writeFile(resolve(profile,'ui-settings.json'),JSON.stringify({version:1,theme:'dark',sidebarWidth:240}));
  const identity={clientId:'synthetic_shell_tabs',token:randomBytes(32).toString('hex'),pipe};
  await writePrivateConfig(configPath,identity);
  await writeFile(resolve(profile,'broker-state.json'),JSON.stringify({version:1,clients:[{id:identity.clientId,name:'Synthetic shell tabs',tokenHash:createHash('sha256').update(identity.token).digest('hex')}],
    workspaces:[{id:'native-default',displayName:'Personal',clients:[identity.clientId]},{id:'shell_saved',displayName:'Saved workspace',clients:[identity.clientId]},{id:'shell_kept',displayName:'Kept workspace',clients:[identity.clientId]}],accountGrants:[],operations:[]}));
  server=http.createServer((request,response)=>{
    const key=new URL(request.url,'http://127.0.0.1').pathname;
    response.setHeader('Cache-Control','no-store');
    if(request.method==='POST'){let body='';request.on('data',bytes=>body+=bytes);request.on('end',()=>{telemetry.set(key,JSON.parse(body));response.end('ok');});return;}
    response.setHeader('Content-Type','text/html;charset=utf-8');
    response.end(`<!doctype html><title>Shell background ${key.slice(1)}</title><input aria-label="Draft"><script>const counts={pointer:0,key:0,focus:0,visibility:0};document.addEventListener('pointerdown',()=>counts.pointer++);document.addEventListener('keydown',()=>counts.key++);window.addEventListener('focus',()=>counts.focus++);document.addEventListener('visibilitychange',()=>counts.visibility++);setInterval(()=>fetch(location.pathname,{method:'POST',body:JSON.stringify({document:performance.timeOrigin,draft:document.querySelector('input').value,counts})}),100)</script>`);
  });await new Promise(r=>server.listen(0,'127.0.0.1',r));base='http://127.0.0.1:'+server.address().port;
  applicationDllSha256=await hash();let client=await launch();
  const worker=await tool(client,'worker_create',{name:'Background shell fixture',workspaceId:'shell_saved'});
  const initialObserver=await tool(client,'worker_create',{name:'Initial Personal observer',workspaceId:'native-default'});
  const initialScope={agentSessionId:initialObserver.agentSessionId,workspaceId:initialObserver.workspaceId};
  const initial=await until(async()=>(await tool(client,'tabs',initialScope)).tabs[0]);await blank(client,initialScope,initial);
  const scope={agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId};
  const tabs=()=>tool(client,'tabs',scope),listing=()=>tool(client,'workspaces');
  const a=await tool(client,'tab_create',{...scope,url:base+'/A'}),b=await tool(client,'tab_create',{...scope,url:base+'/B'});
  await until(()=>telemetry.has('/B'));
  const before=structuredClone(telemetry.get('/B'));
  await check('Visible X closes a background tab without selecting its page',async()=>{
    await ready('Click the X on background tab Shell background A, leaving Personal selected.');
    await until(async()=>!(await tabs()).tabs.some(tab=>tab.tabId===a.tabId),300000);
    await sleep(300);assert.deepEqual(telemetry.get('/B'),before);assert((await tabs()).tabs.some(tab=>tab.tabId===b.tabId));return {survivingPageUnchanged:true};
  });
  await check('Middle click closes the last tab and keeps its workspace',async()=>{
    await ready('Middle-click background tab Shell background B.');await until(async()=>!(await tabs()).tabs.length,300000);
    assert((await listing()).workspaces.some(w=>w.workspaceId==='shell_saved'&&w.displayName==='Saved workspace'));return {workspaceRetained:true};
  });
  await check('Selecting an empty saved workspace opens a blank human-owned tab',async()=>{
    await ready('Click Saved workspace in the sidebar.');
    const tab=await until(async()=>(await tabs()).tabs.find(tab=>tab.ownerSessionId==='human'),300000);await blank(client,scope,tab);return {humanOwned:true,url:'about:blank'};
  });
  await check('Native Exit persists saved workspaces',async()=>{await exitNormally(client);return {exitCode:0};});
  client=await launch();const observer=await tool(client,'worker_create',{name:'Restart shell observer',workspaceId:'shell_kept'});
  const observedScope={agentSessionId:observer.agentSessionId,workspaceId:observer.workspaceId};
  const personal=await tool(client,'worker_create',{name:'Personal observer',workspaceId:'native-default'});
  await check('Restart retains names and opens only one Personal blank tab',async()=>{
    const names=(await tool(client,'workspaces')).workspaces.map(w=>w.displayName);assert(names.includes('Saved workspace')&&names.includes('Kept workspace'));
    const personalScope={agentSessionId:personal.agentSessionId,workspaceId:personal.workspaceId};const initial=await until(async()=>{const tabs=(await tool(client,'tabs',personalScope)).tabs;return tabs.length?tabs:false;});assert.equal(initial.length,1);await blank(client,personalScope,initial[0]);
    assert.equal((await tool(client,'tabs',observedScope)).tabs.length,0);return {namesRetained:true,automaticOldPageLoads:0};
  });
  await check('Saved empty workspace remains selectable after restart',async()=>{
    await ready('Verify both saved workspace labels remain visible, then click Kept workspace.');
    const tab=await until(async()=>(await tool(client,'tabs',observedScope)).tabs[0],300000);assert.equal(tab.ownerSessionId,'human');await blank(client,observedScope,tab);return {humanOwned:true};
  });
  await check('Ctrl+W closes the selected tab without deleting its workspace',async()=>{
    await ready('Press Ctrl+W with the Kept workspace blank tab selected.');await until(async()=>!(await tool(client,'tabs',observedScope)).tabs.length,300000);
    assert((await tool(client,'workspaces')).workspaces.some(w=>w.workspaceId==='shell_kept'));return {workspaceRetained:true};
  });
  await check('Final native Exit completes normally',async()=>{await exitNormally(client);return {exitCode:0};});
}catch(error){console.log('FAIL '+error.message);if(!results.some(r=>!r.passed))results.push({name:'Shell harness',passed:false,error:error.message});}
finally{
  for(const client of clients)await client.close().catch(()=>{});
  if(browser?.pid&&browser.exitCode===null&&browser.signalCode===null)await runFile('taskkill.exe',['/PID',String(browser.pid),'/T','/F'],{windowsHide:true}).catch(()=>{});
  if(server){server.closeAllConnections();await new Promise(r=>server.close(r));}
  const applicationDllSha256AtEnd=await hash();if(applicationDllSha256AtEnd!==applicationDllSha256)results.push({name:'Binary stability',passed:false});
  const report={run,capturedAt:new Date().toISOString(),binary:'build/app/Release/Xenon.exe',applicationDllSha256,applicationDllSha256AtEnd,profile,uiInput:'native Windows actions performed separately by operator',passed:results.length===8&&results.every(r=>r.passed),results};
  await writeFile(resolve(root,'out/shell-tabs-integration-results.json'),JSON.stringify(report,null,2));process.exitCode=report.passed?0:1;
}
