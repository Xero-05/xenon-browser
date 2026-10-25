// Production CEF with loopback pages and a disposable profile. Native messages
// exercise human toolbar/dialog routes; no production test driver is used.
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

const root=resolve(import.meta.dirname,'..'),run=`native-page-controls-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile=resolve(root,'.cache',run),build=resolve(root,process.env.XENON_TEST_BUILD_DIR??'build');
const binary=resolve(build,'app/Release/Xenon.exe'),file=promisify(execFile),sleep=ms=>new Promise(r=>setTimeout(r,ms));
const filter=process.argv.find(a=>a.startsWith('--case='))?.slice(7),checks=[],events=[],requests=[],samples=[];
let browser,server,client,startHashes,endHashes,failure,metrics;
const hashes=async()=>Object.fromEntries(await Promise.all(['Xenon.exe','Xenon.dll','libcef.dll'].map(async name=>[name,createHash('sha256').update(await readFile(resolve(binary,'..',name))).digest('hex')])));
async function until(fn,label,ms=10000){const end=Date.now()+ms;while(Date.now()<end){const value=await fn();if(value)return value;await sleep(150);}throw Error(`${label} timed out`);}
async function native(action='snapshot',extra=[]){const state=JSON.parse((await file('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',resolve(root,'tests/fixtures/native-page-controls.ps1'),'-BrowserPid',String(browser.pid),'-Profile',profile,'-Action',action,...extra],{windowsHide:true,timeout:20000})).stdout);samples.push({action,state});return state;}
async function click(id,action='click'){const box=metrics.boxes[id];assert(box,`Missing fixture layout: ${id}`);return native(action,['-X',String(Math.round((box.x+box.width/2)*metrics.dpr)),'-Y',String(Math.round((box.y+box.height/2)*metrics.dpr))]);}
async function check(name,fn){if(filter&&!name.toLowerCase().includes(filter))return;try{const detail=await fn();checks.push({name,passed:true,detail});console.log(`PASS ${name}`);}catch(error){checks.push({name,passed:false,error:error.message});console.error(`FAIL ${name}: ${error.message}`);}}
const pageCount=()=>requests.filter(path=>path==='/').length;
async function confirmation(){return until(async()=>{const state=await native();return state.dialogReady&&state;},'Native reload confirmation');}
const popup=state=>state.popups.find(w=>w.visible&&w.className==='Chrome_WidgetWin_1');
function contained(bounds,work){return bounds.left>=work.left&&bounds.top>=work.top&&bounds.right<=work.right&&bounds.bottom<=work.bottom;}
const raw=(name,args)=>client.callTool({name:`xenon_${name}`,arguments:args});
async function tool(name,args){const result=await raw(name,args);assert(!result.isError,`${name}: ${JSON.stringify(result.structuredContent)}`);return result.structuredContent;}
try{
  await mkdir(profile,{recursive:true});await mkdir(resolve(root,'out'),{recursive:true});
  await writeFile(resolve(profile,'SYNTHETIC_TEST_PROFILE'),'XENON_SYNTHETIC_NATIVE_PAGE_CONTROLS\n');
  await writeFile(resolve(profile,'ui-settings.json'),JSON.stringify({version:1,introductionCompleted:true,automaticUpdateChecks:false,language:'en'}));
  const pipeName=`xenon-test-${run}`,pipe=`\\\\.\\pipe\\${pipeName}`,config=resolve(profile,'client.json');
  const identity={clientId:`controls_${randomBytes(8).toString('hex')}`,token:randomBytes(32).toString('hex'),pipe};
  await writeFile(resolve(profile,'broker-state.json'),JSON.stringify({version:1,clients:[{id:identity.clientId,name:'Synthetic native controls client',tokenHash:createHash('sha256').update(identity.token).digest('hex')}],workspaces:[{id:'native-default',clients:[identity.clientId]}],accountGrants:[],operations:[]}));
  await writePrivateConfig(config,identity);
  server=http.createServer((req,res)=>{
    if(req.url==='/event'){let body='';req.on('data',c=>body+=c);req.on('end',()=>{const event=JSON.parse(body);events.push(event);if(event.type==='layout')metrics=event;res.writeHead(204).end();});return;}
    if(req.url!=='/'){res.writeHead(404).end();return;}
    requests.push(req.url);
    res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Cache-Control':'no-store'}).end(`<!doctype html><title>Synthetic native page controls</title>
      <style>body{background:white;font:18px Arial}button{position:absolute;left:20px;width:220px;height:40px}#arm{top:20px}#confirm{top:80px}#rtl{top:130px}#wide{top:230px}select{position:absolute;left:200px;top:180px;width:220px;font:18px Arial}</style>
      <button id="arm">Arm unsaved changes</button><button id="confirm">Ordinary confirmation</button>
      <button id="rtl">Right-aligned menu</button><button id="wide">Oversized menu</button>
      <select id="documents"><option value="first">Synthetic document ${'Long document name '.repeat(12)} first</option><option value="second">Synthetic second document</option></select>
      <script>
      const emit=(type,data={})=>fetch('/event',{method:'POST',body:JSON.stringify({type,...data})});
      const layout=()=>emit('layout',{dpr:devicePixelRatio,boxes:Object.fromEntries([...document.querySelectorAll('button,select')].map(e=>[e.id,e.getBoundingClientRect().toJSON()]))});
      addEventListener('load',layout);addEventListener('resize',layout);
      document.querySelector('#arm').onclick=()=>{onbeforeunload=e=>{emit('beforeunload');e.preventDefault();e.returnValue='';};emit('armed');};
      document.querySelector('#confirm').onclick=()=>emit('confirmed',{accepted:confirm('Synthetic ordinary confirmation')});
      document.querySelector('#documents').onchange=e=>emit('selected',{value:e.target.value});
      document.addEventListener('click',e=>emit('click',{id:e.target.id,focused:document.hasFocus(),trusted:e.isTrusted}));
      for(const mode of ['rtl','wide'])document.querySelector('#'+mode).onclick=()=>{const select=document.querySelector('#documents');select.dir=mode==='rtl'?'rtl':'ltr';select.selectedIndex=0;if(mode==='wide')select.options[0].textContent='Synthetic document '+ 'Very long document name '.repeat(40);layout();emit('mode',{mode});};
      </script>`);
  });
  await new Promise(r=>server.listen(0,'127.0.0.1',r));startHashes=await hashes();
  const base=`http://127.0.0.1:${server.address().port}/`;
  browser=spawn(binary,[`--user-data-dir=${profile}`,`--broker-pipe=${pipeName}`,base],{stdio:'ignore',windowsHide:true});
  let launchError;browser.once('error',error=>launchError=error);
  await until(()=>{if(launchError)throw launchError;if(browser.exitCode!==null)throw Error(`Fixture exited: ${browser.exitCode}`);return metrics;},'Fixture startup',45000);
  await check('Reload of an ordinary page reaches the website',async()=>{
    const before=pageCount();await native('reload');await until(()=>pageCount()>before,'Reload request');return {before,after:pageCount()};
  });
  await check('Reload confirmation can cancel and F5 can confirm without an automatic retry',async()=>{
    await click('confirm');await confirmation();await native('dismiss');
    await until(()=>events.some(e=>e.type==='confirmed'&&!e.accepted),'Ordinary confirmation dismissal');
    await click('arm');await until(()=>events.some(e=>e.type==='armed'),'Unsaved changes handler');
    const before=pageCount();await native('reload');const canceled=await confirmation();
    assert.equal(pageCount(),before,'No reload before the human chooses');assert(canceled.dialogMessage.includes('127.0.0.1'),'Confirmation identifies its website');
    await native('dismiss');await sleep(400);assert.equal(pageCount(),before,'Dismiss preserves the current document');
    await native('f5');const accepted=await confirmation();assert.equal(pageCount(),before,'F5 waits for confirmation');
    await native('accept');await until(()=>pageCount()>before,'Accepted F5 reload');assert.equal(pageCount(),before+1,'Exactly one accepted reload request');
    assert(!(await native()).dialogReady,'Answered confirmation is cleared');
    return {canceled,accepted,before,after:pageCount()};
  });
  await check('Long dropdown stays inside its monitor work area and remains selectable',async()=>{
    const placed=await native('place',['-X','120','-Y','80','-Width','1100','-Height','700']);
    const menus=[];
    for(const mode of ['ltr','rtl','wide']){
      await native('foreground');
      if(mode!=='ltr'){await click(mode);await until(()=>events.some(e=>e.type==='mode'&&e.mode===mode),`${mode} fixture mode`);}
      const before=events.length,opened=await click('documents','dropdown');
      assert(popup(opened),`${mode} native dropdown missing: ${JSON.stringify(opened)}`);
      assert(opened.foregroundOwn,`${mode} fixture lost foreground during popup selection`);
      const bounds=popup(opened).bounds;
      assert(contained(bounds,opened.work),`${mode} dropdown is clipped: ${JSON.stringify({bounds,work:opened.work})}`);
      await until(()=>events.slice(before).some(e=>e.type==='selected'&&e.value==='second'),`${mode} dropdown website selection`);
      assert(!popup(await native()),'Selection dismisses the popup');menus.push({mode,opened,selected:'second'});
    }
    return {placed,menus};
  });
  await check('Ownership and human pause protect pending reload confirmations',async()=>{
    await until(()=>new Promise(done=>{const socket=net.connect(pipe);socket.once('connect',()=>{socket.destroy();done(true);});socket.once('error',()=>{socket.destroy();done(false);});}),'Broker startup');
    client=new Client({name:'Native page controls regression',version:'1'},{versionNegotiation:{mode:{pin:'2026-07-28'}}});
    await client.connect(new StdioClientTransport({command:process.execPath,args:[resolve(root,'adapter/dist/src/cli.js'),'serve','--config',config],stderr:'pipe'}));
    const worker=await tool('worker_create',{name:'Synthetic controls worker',workspaceId:'native-default'}),beforeEvents=events.length;
    const tab=await tool('tab_create',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,url:base});
    const scope={agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,tabId:tab.tabId};
    await until(()=>events.slice(beforeEvents).some(e=>e.type==='layout'),'Agent fixture document');
    await file('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',resolve(root,'tests/fixtures/select-shell-tab.ps1'),'-ProcessId',String(browser.pid),'-Index','1'],{windowsHide:true});
    await native('foreground');
    await click('arm');await until(()=>events.slice(beforeEvents).some(e=>e.type==='armed'),'Agent form activation');
    const owner=await tool('control_status',scope),before=pageCount();samples.push({label:'Agent tab selected',owner,tabs:await tool('tabs',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId})});await native('reload');await confirmation();
    const held=await tool('control_status',scope);assert(held.humanPaused,'Human confirmation holds the pause');
    assert.equal(held.ownerSessionId,owner.ownerSessionId);assert.equal(held.ownershipGeneration,owner.ownershipGeneration);
    const denied=await raw('dialog',{...scope,ownershipGeneration:held.ownershipGeneration,operationId:randomUUID(),accept:true});
    assert(denied.isError&&denied.structuredContent?.error?.code==='HUMAN_INPUT_PAUSED','Agent cannot answer the human reload confirmation');
    assert.equal(pageCount(),before,'Denied agent answer has no reload effect');await native('dismiss');
    await until(async()=>!(await tool('control_status',scope)).humanPaused,'Native dialog cooldown');
    await tool('observe',{...scope,maxNodes:100});
    await tool('navigate',{...scope,ownershipGeneration:held.ownershipGeneration,operationId:randomUUID(),action:'reload'});
    const observed=await until(async()=>{const result=await tool('observe',{...scope,maxNodes:100});return result.dialog?.pending&&result;},'Agent reload confirmation');
    assert.equal(observed.dialog.type,'confirm');assert(!(await tool('control_status',scope)).humanPaused,'Agent confirmation does not create a human pause');
    await tool('dialog',{...scope,ownershipGeneration:held.ownershipGeneration,operationId:randomUUID(),accept:false});
    assert.equal(pageCount(),before,'Both dismissed reload attempts preserve the document');
    return {ownerSessionId:held.ownerSessionId,ownershipGeneration:held.ownershipGeneration,humanHeld:true,agentAnswerDenied:true,agentDialogType:observed.dialog.type};
  });
}catch(error){failure=error.message;console.error(error);}
finally{
  await client?.close().catch(()=>{});
  if(browser?.pid&&browser.exitCode===null)await file('taskkill.exe',['/PID',String(browser.pid),'/T','/F'],{windowsHide:true}).catch(()=>{});
  server?.closeAllConnections();server?.close();endHashes=await hashes().catch(()=>undefined);
  const passed=!failure&&(filter?checks.length>0:checks.length===4)&&checks.every(c=>c.passed)&&JSON.stringify(startHashes)===JSON.stringify(endHashes);
  const report=resolve(root,'out',`${run}.json`);
  await writeFile(report,JSON.stringify({run,binary,startHashes,endHashes,checks,samples,events,requests,failure,passed,coverage:'Synthetic production browser and external Win32 messages, disposable profile only. Not physical input, real portal or mixed-DPI/multi-monitor acceptance.'},null,2));
  console.log(`Report: ${report}`);if(!passed)process.exitCode=1;
}
