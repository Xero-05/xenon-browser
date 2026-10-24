// Production-browser reproduction of workflow viewport/control/dropdown failures.
// Uses only a generated profile, loopback pages and supported MCP page actions.
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

const root=resolve(import.meta.dirname,'..'),run=`workflow-viewport-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile=resolve(root,'.cache',run),pipeName=`xenon-test-${run}`,pipe=`\\\\.\\pipe\\${pipeName}`;
const build=resolve(root,process.env.XENON_TEST_BUILD_DIR??'build'),binary=resolve(build,'app/Release/Xenon.exe');
const config=resolve(profile,'client.json'),runFile=promisify(execFile),sleep=ms=>new Promise(r=>setTimeout(r,ms));
const checks=[],samples=[],events=[];let server,browser,client,worker,base,startHashes,endHashes,failure;
const caseFilter=process.argv.find(a=>a.startsWith('--case='))?.slice(7);
const hashes=async()=>Object.fromEntries(await Promise.all(['Xenon.exe','Xenon.dll','libcef.dll'].map(async name=>[name,createHash('sha256').update(await readFile(resolve(binary,'..',name))).digest('hex')])));
async function until(fn,label,ms=15000){const end=Date.now()+ms;while(Date.now()<end){const value=await fn();if(value)return value;await sleep(150);}throw Error(`${label} timed out`);}
const alive=()=>new Promise(done=>{const s=net.connect(pipe);s.once('connect',()=>{s.destroy();done(true);});s.once('error',()=>{s.destroy();done(false);});s.setTimeout(1000,()=>{s.destroy();done(false);});});
const scope=tab=>({agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,tabId:tab.tabId});
const raw=(name,args={})=>client.callTool({name:`xenon_${name}`,arguments:args});
async function tool(name,args={}){const r=await raw(name,args);assert(!r.isError,`${name}: ${JSON.stringify(r.structuredContent)}`);return r.structuredContent;}
async function windowState(action='snapshot',extra=[]){return JSON.parse((await runFile('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',resolve(root,'tests/fixtures/workflow-window.ps1'),'-BrowserPid',String(browser.pid),'-Profile',profile,'-Action',action,...extra],{windowsHide:true})).stdout);}
async function popupState(){return JSON.parse((await runFile('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',resolve(root,'tests/fixtures/popup-windows.ps1'),'-ProcessId',String(browser.pid)],{windowsHide:true})).stdout);}
async function selectTab(index){await runFile('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',resolve(root,'tests/fixtures/select-shell-tab.ps1'),'-ProcessId',String(browser.pid),'-Index',String(index)],{windowsHide:true});}
function sample(label,r){const o=r.structuredContent??{};samples.push({label,error:o.error,viewport:o.viewport,loading:o.loading,nodeCount:o.nodes?.length,controls:o.nodes?.filter(n=>['INPUT','SELECT','BUTTON'].includes(n.tag)).map(n=>({tag:n.tag,name:n.name,ref:!!n.ref,visibilityStatus:n.visibilityStatus,reason:n.reason})),visibility:o.visibility});}
async function observe(tab,label){return until(async()=>{const r=await raw('observe',{...scope(tab),maxNodes:200});sample(label,r);if(r.isError){if(['privacy_guard_initializing','page_changed','human_active'].includes(r.structuredContent?.error?.code))return false;throw Error(JSON.stringify(r.structuredContent));}return !r.structuredContent.loading?r.structuredContent:false;},label);}
async function healthy(tab,label){
  const o=await observe(tab,label);
  assert(o.viewport?.width>0&&o.viewport?.height>0,`${label}: zero viewport ${JSON.stringify(o.viewport)}`);
  assert.equal(o.nodes.filter(n=>n.tag==='SELECT'&&n.ref).length,4,`${label}: visible selectors missing: ${JSON.stringify(o.nodes.filter(n=>n.tag==='SELECT'))}`);
  assert(o.nodes.some(n=>n.tag==='INPUT'&&n.ref),`${label}: visible text field missing`);
  const shot=await until(async()=>{const r=await raw('screenshot',scope(tab));if(r.isError){sample(`${label} capture attempt`,r);const error=r.structuredContent?.error;if(['privacy_guard_initializing','page_changed','screenshot_unverified'].includes(error?.code)||(error?.code==='screenshot_failed'&&error.message==='The image does not cover the measured CSS viewport.'))return false;}return r;},`${label} screenshot`);
  assert(!shot.isError,`${label}: screenshot ${JSON.stringify(shot.structuredContent)}`);
  assert(shot.content.some(c=>c.type==='image'),`${label}: no image`);
  return {viewport:o.viewport,selectors:4};
}
async function act(tab,action,extra=()=>({})){const o=await observe(tab,action),status=await tool('control_status',scope(tab));const result=await tool('interact',{...scope(tab),ownershipGeneration:status.ownershipGeneration,operationId:randomUUID(),observationId:o.observationId,action,...extra(o)});samples.push({action,finishedAt:Date.now(),events:events.filter(e=>e.key===tab.key)});return result;}
async function check(name,fn){if(caseFilter&&!name.toLowerCase().includes(caseFilter))return;try{const detail=await fn();checks.push({name,passed:true,detail});console.log(`PASS ${name}`);}catch(e){checks.push({name,passed:false,error:e.message});console.error(`FAIL ${name}: ${e.message}`);}}
async function createPage(){const key=randomUUID();const tab=await tool('tab_create',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,url:`${base}/?key=${key}`});await until(()=>events.some(e=>e.key===key&&e.type==='ready'),'Fixture ready');return {...tab,key};}
try{
  await mkdir(profile,{recursive:true});await mkdir(resolve(root,'out'),{recursive:true});
  await writeFile(resolve(profile,'SYNTHETIC_TEST_PROFILE'),'XENON_SYNTHETIC_WORKFLOW_VIEWPORT_FIXTURE\n');
  await writeFile(resolve(profile,'ui-settings.json'),JSON.stringify({version:1,introductionCompleted:true,automaticUpdateChecks:false}));
  const identity={clientId:`workflow_${randomBytes(8).toString('hex')}`,token:randomBytes(32).toString('hex'),pipe};
  await writeFile(resolve(profile,'broker-state.json'),JSON.stringify({version:1,clients:[{id:identity.clientId,name:'Synthetic workflow client',tokenHash:createHash('sha256').update(identity.token).digest('hex')}],workspaces:[{id:'native-default',clients:[identity.clientId]}],accountGrants:[],operations:[]}));
  await writePrivateConfig(config,identity);
  server=http.createServer((req,res)=>{
    if(req.url==='/event'){let body='';req.on('data',c=>body+=c);req.on('end',()=>{events.push({...JSON.parse(body),receivedAt:Date.now()});res.writeHead(204).end();});return;}
    const url=new URL(req.url,'http://127.0.0.1'),key=url.searchParams.get('key')??'popup';
    res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Cache-Control':'no-store'}).end(`<!doctype html><title>Synthetic workflow controls</title><style>body{font:16px sans-serif;background:white;color:black;margin:20px}label{display:block;margin:12px}select,input{min-width:240px;padding:4px}button{padding:8px}</style><h1>Workflow fixture ready</h1><label>Package name <input></label>${['Resume','Letter','Transcript','Supplement'].map((label,i)=>`<label>${label} <select id="s${i}"><option value="">--select--</option><option value="first">Synthetic first document</option><option value="second">Synthetic second document</option></select></label>`).join('')}<button id="popup">Open posting popup</button><script>
      const key=${JSON.stringify(key)},emit=(type,extra={})=>fetch('/event',{method:'POST',body:JSON.stringify({key,type,width:innerWidth,height:innerHeight,...extra})});
      addEventListener('load',()=>emit('ready'));addEventListener('resize',()=>emit('resize'));
      for(const e of document.querySelectorAll('select,input'))for(const type of ['click','input','change','focus','blur','keydown','keyup'])e.addEventListener(type,event=>emit(type,{id:e.id,value:e.value,keyPressed:event.key,focused:document.activeElement?.id,documentFocused:document.hasFocus()}));
      const first=document.querySelector('#s0');for(const type of ['mouseenter','focus'])first.addEventListener(type,()=>{const text=first.parentElement.firstChild,range=document.createRange();range.selectNodeContents(text);const full=range.getBoundingClientRect();range.setEnd(text,text.textContent.trimEnd().length);const trimmed=range.getBoundingClientRect(),box=first.getBoundingClientRect(),style=getComputedStyle(first);emit('label-geometry',{fullRight:full.right,trimmedRight:trimmed.right,controlLeft:box.left,outlineStyle:style.outlineStyle,outlineWidth:style.outlineWidth,outlineOffset:style.outlineOffset});});
      document.querySelector('#popup').onclick=()=>window.open('/?key=popup','_blank','width=600,height=500');</script>`);
  });
  await new Promise(r=>server.listen(0,'127.0.0.1',r));base=`http://127.0.0.1:${server.address().port}`;startHashes=await hashes();
  browser=spawn(binary,[`--user-data-dir=${profile}`,`--broker-pipe=${pipeName}`],{stdio:'ignore',windowsHide:true});
  let launchError;browser.once('error',e=>launchError=e);
  await until(()=>{if(launchError)throw launchError;if(browser.exitCode!==null)throw Error('Fixture browser exited');return alive();},'Startup',45000);
  client=new Client({name:'Workflow viewport regression',version:'1'},{versionNegotiation:{mode:{pin:'2026-07-28'}}});
  await client.connect(new StdioClientTransport({command:process.execPath,args:[resolve(root,'adapter/dist/src/cli.js'),'serve','--config',config],stderr:'pipe'}));
  worker=await tool('worker_create',{name:'Workflow fixture worker',workspaceId:'native-default'});
  const tab=await createPage();
  await check('Covered newly created tab has usable viewport, controls and screenshot',()=>healthy(tab,'covered creation'));
  await check('Background select click and ArrowDown preserve supported input',async()=>{
    const target=o=>({elementRef:o.nodes.find(n=>n.tag==='SELECT'&&n.ref)?.ref});
    samples.push({label:'native before dropdown',state:await windowState()});
    await act(tab,'click',target);const click=await popupState();
    samples.push({label:'native after dropdown',state:await windowState()});
    assert(!click.windows.some(w=>w.startsWith('Chrome_WidgetWin_1')),'Covered select must not create a desktop popup');
    await act(tab,'key',()=>({key:'ArrowDown'}));
    await until(()=>events.some(e=>e.key===tab.key&&e.id==='s0'&&e.type==='change'&&e.value==='first'),'ArrowDown website value',2500);
    return {click,arrowDownChangedValue:true};
  });
  await check('Selected foreground form supports screenshot-bound native dropdown input',async()=>{
    await selectTab(1);await windowState('foreground');await healthy(tab,'foreground form');
    const evidence=await tool('inspect',{...scope(tab),maxNodes:200});
    const control=evidence.nodes.find(n=>n.tag==='SELECT'&&n.ref),box=control.bounds;
    const status=await tool('control_status',scope(tab));
    await tool('interact',{...scope(tab),ownershipGeneration:status.ownershipGeneration,operationId:randomUUID(),observationId:evidence.observationId,action:'click',x:box.x+box.width/2-evidence.viewport.pageX,y:box.y+box.height/2-evidence.viewport.pageY});
    const state=await popupState();samples.push({label:'foreground popup state',state});assert(state.foregroundOwn,'Fixture lost foreground');
    assert(state.windows.some(w=>w.startsWith('Chrome_WidgetWin_1')),'Selected foreground click must open native dropdown');
    await act(tab,'key',()=>({key:'Escape'}));return state;
  });
  await check('Minimize is honest and restore recovers existing and newly created tabs',async()=>{
    const minimized=await windowState('minimize');await sleep(250);
    const o=await observe(tab,'minimized existing'),shot=await raw('screenshot',scope(tab));sample('minimized screenshot',shot);
    if(o.viewport?.width<=0||o.viewport?.height<=0){assert(shot.isError,'Invalid viewport must not issue screenshot evidence');assert(!shot.content.some(c=>c.type==='image'));}
    const created=await createPage();const newObservation=await observe(created,'minimized creation');
    const restored=await windowState('restore');await sleep(250);
    const existing=await healthy(tab,'restored existing'),fresh=await healthy(created,'restored new');
    samples.push({label:'minimized native geometry',minimized,restored});
    assert(o.viewport.width>0&&o.viewport.height>0,'Minimization collapsed the existing tab viewport');
    assert(newObservation.viewport.width>0&&newObservation.viewport.height>0,'Tab created while minimized has a collapsed viewport');
    assert.equal(o.nodes.filter(n=>n.tag==='SELECT'&&n.ref).length,4,'Existing minimized tab lost its visible controls');
    assert.equal(newObservation.nodes.filter(n=>n.tag==='SELECT'&&n.ref).length,4,'New minimized tab lost its visible controls');
    assert(!shot.isError&&shot.content.some(c=>c.type==='image'),'Minimized tab must remain capturable');
    return {minimized,restored,minimizedViewport:o.viewport,screenshotError:shot.structuredContent?.error,existing,fresh};
  });
  await check('Resize and covered-tab creation keep ordinary controls observable',async()=>{
    const details=[];
    for(const [width,height] of [[900,700],[1400,950],[1100,800]]){
      details.push(await windowState('snapshot',['-Width',String(width),'-Height',String(height)]));
      details.push(await healthy(tab,`resized ${width}x${height}`));
      const next=await createPage();details.push(await healthy(next,`covered resized ${width}x${height}`));
    }
    return details;
  });
  await check('Window-open popup receives usable viewport and ordinary controls',async()=>{
    const before=await tool('tabs',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId});
    await act(tab,'click',o=>({elementRef:o.nodes.find(n=>n.tag==='BUTTON'&&n.name==='Open posting popup'&&n.ref).ref}));
    const popup=await until(async()=>{const tabs=await tool('tabs',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId});return tabs.tabs?.find(t=>!before.tabs.some(old=>old.tabId===t.tabId));},'Popup registration');
    await until(()=>events.some(e=>e.key==='popup'&&e.type==='ready'),'Popup fixture ready');
    return healthy(popup,'posting popup');
  });
}catch(e){failure=e.message;console.error(e);}
finally{
  await client?.close().catch(()=>{});
  if(browser?.pid&&browser.exitCode===null)await runFile('taskkill.exe',['/PID',String(browser.pid),'/T','/F'],{windowsHide:true}).catch(()=>{});
  server?.closeAllConnections();server?.close();endHashes=await hashes().catch(()=>undefined);
  const passed=!failure&&checks.length===(caseFilter?1:6)&&checks.every(c=>c.passed)&&JSON.stringify(startHashes)===JSON.stringify(endHashes);
  await mkdir(resolve(root,'out'),{recursive:true});
  await writeFile(resolve(root,'out',`${run}.json`),JSON.stringify({run,binary,startHashes,endHashes,checks,samples,events,failure,passed,coverage:'Production browser, loopback pages and native messages scoped to the generated fixture process. No personal profile, real portal, physical input or native test-driver bypass.'},null,2));
  console.log(`Report: out/${run}.json`);if(!passed)process.exitCode=1;
}
