// Production browser with a disposable profile and synthetic loopback page.
// A Chromium select/datalist popup must never escape a covered agent tab.
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

const root=resolve(import.meta.dirname,'..'),run=`background-popup-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile=resolve(root,'.cache',run),pipeName=`xenon-test-${run}`,pipe=`\\\\.\\pipe\\${pipeName}`;
const binary=resolve(root,'build/app/Release/Xenon.exe'),configPath=resolve(profile,'client-config.json');
const runFile=promisify(execFile),sleep=ms=>new Promise(r=>setTimeout(r,ms));
const hashes=async()=>Object.fromEntries(await Promise.all(['Xenon.dll','libcef.dll'].map(async n=>[n,createHash('sha256').update(await readFile(resolve(binary,'..',n))).digest('hex')])));
const checks=[];let browser,client,server,failure,startHashes,endHashes;
const events=[];
async function until(fn,label,ms=30_000){const end=Date.now()+ms;while(Date.now()<end){const value=await fn();if(value)return value;await sleep(150);}throw Error(`${label} timed out`);}
const alive=()=>new Promise(done=>{const s=net.connect(pipe);s.once('connect',()=>{s.destroy();done(true);});s.once('error',()=>{s.destroy();done(false);});s.setTimeout(1000,()=>{s.destroy();done(false);});});
async function tool(name,args){const v=await client.callTool({name:`xenon_${name}`,arguments:args});assert(!v.isError,`${name}: ${JSON.stringify(v.structuredContent)}`);return v.structuredContent;}
const driver=async(file,args=[])=>JSON.parse((await runFile('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',resolve(root,'tests/fixtures',file),'-ProcessId',String(browser.pid),...args],{windowsHide:true})).stdout);
const fixture=`<!doctype html><meta charset="utf-8"><title>Background popup fixture</title>
<label>Choose a number <select aria-label="Choose a number"><option>149935</option><option>149279</option><option>150027</option></select></label>
<label>Number suggestions <input aria-label="Number suggestions" list="numbers"></label>
<datalist id="numbers"><option value="149935"><option value="149279"><option value="150027"></datalist>
<button id="regular">Regular action</button>
<button id="suggestions">Open suggestions</button>
<script>for(const e of document.querySelectorAll('select,input,button'))for(const type of ['click','input','change'])e.addEventListener(type,()=>fetch('/event',{method:'POST',body:JSON.stringify({tag:e.tagName,id:e.id,type,value:e.value||''})}));document.querySelector('#suggestions').onclick=()=>{const e=document.querySelector('input');e.value='149';e.focus();try{e.showPicker();}catch(error){fetch('/event',{method:'POST',body:JSON.stringify({type:'picker-error',value:error.name})});}};</script>`;
try{
  await mkdir(profile,{recursive:true});
  const identity={clientId:`popup_${randomBytes(8).toString('hex')}`,token:randomBytes(32).toString('hex'),pipe};
  await writeFile(resolve(profile,'SYNTHETIC_TEST_PROFILE'),'XENON_SYNTHETIC_POPUP_FIXTURE\n');
  await writeFile(resolve(profile,'ui-settings.json'),JSON.stringify({version:1,introductionCompleted:true}));
  await writeFile(resolve(profile,'broker-state.json'),JSON.stringify({version:1,clients:[{id:identity.clientId,name:'Synthetic popup client',tokenHash:createHash('sha256').update(identity.token).digest('hex')}],workspaces:[{id:'native-default',clients:[identity.clientId]}],accountGrants:[],operations:[]}));
  await writePrivateConfig(configPath,identity);
  server=http.createServer((req,res)=>{if(req.method==='POST'&&req.url==='/event'){let body='';req.on('data',c=>body+=c);req.on('end',()=>{events.push(JSON.parse(body));res.writeHead(204).end();});}else res.writeHead(200,{'Content-Type':'text/html'}).end(fixture);});
  await new Promise(r=>server.listen(0,'127.0.0.1',r));startHashes=await hashes();
  browser=spawn(binary,[`--user-data-dir=${profile}`,`--broker-pipe=${pipeName}`],{stdio:'ignore'});
  let launchError;browser.once('error',e=>launchError=e);
  await until(()=>{if(launchError)throw launchError;if(browser.exitCode!==null)throw Error('Browser exited');return alive();},'Startup',45000);
  client=new Client({name:'Background popup regression',version:'1'},{versionNegotiation:{mode:{pin:'2026-07-28'}}});
  await client.connect(new StdioClientTransport({command:process.execPath,args:[resolve(root,'adapter/dist/src/cli.js'),'serve','--config',configPath],stderr:'pipe'}));
  const worker=await tool('worker_create',{name:'Popup observer',workspaceId:'native-default'});
  const tab={...worker,...await tool('tab_create',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,url:`http://127.0.0.1:${server.address().port}/`})};
  const scope={agentSessionId:tab.agentSessionId,workspaceId:tab.workspaceId,tabId:tab.tabId};
  let lastObservation;
  const observe=()=>until(async()=>{const v=await client.callTool({name:'xenon_observe',arguments:{...scope,maxNodes:100}});if(v.isError){if(['privacy_guard_initializing','page_changed','human_active'].includes(v.structuredContent?.error?.code))return false;throw Error(JSON.stringify(v.structuredContent));}lastObservation=v.structuredContent;return v.structuredContent.nodes?.some(n=>n.tag==='SELECT'&&n.ref)?v.structuredContent:false;},'Controls').catch(e=>{console.log(JSON.stringify(lastObservation));throw e;});
  const act=async(name,action,extra={})=>{const o=await observe(),tag=({ 'Choose a number':'SELECT','Number suggestions':'INPUT' }[name]||'BUTTON'),node=o.nodes.find(n=>n.tag===tag&&(tag!=='BUTTON'||n.name===name)&&n.ref);assert(node,JSON.stringify(o.nodes));return tool('interact',{...scope,ownershipGeneration:tab.ownershipGeneration,operationId:randomUUID(),observationId:o.observationId,action,elementRef:node.ref,...extra});};
  await observe();
  const baseline=(await driver('popup-windows.ps1')).windows;
  for(const [name,action,extra] of [['Choose a number','click',{}],['Open suggestions','click',{}]]){
    await act(name,action,extra);await sleep(500);
    const {windows,capture}=await driver('popup-windows.ps1'),escaped=windows.filter(n=>!baseline.includes(n));
    const result={name,windows,escaped,capture};checks.push(result);console.log(JSON.stringify(result));
    assert.equal(escaped.length,0,`${name}: background popup escaped above the selected tab`);
    assert.equal(capture,'',`${name}: hidden popup captured the human pointer`);
  }
  await until(()=>events.some(e=>e.id==='suggestions'&&e.type==='click'),'Suggestion picker button');
  assert(!events.some(e=>e.type==='picker-error'),'Synthetic suggestion picker threw');
  await act('Choose a number','select',{values:['149279']});
  await until(()=>events.some(e=>e.tag==='SELECT'&&e.type==='change'&&e.value==='149279'),'Background selection');
  await act('Regular action','click');
  await until(()=>events.some(e=>e.id==='regular'&&e.type==='click'),'Background button');
  checks.push({backgroundSelection:true,backgroundClick:true});
  if(process.argv.includes('--foreground')){
  const select=async index=>runFile('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',resolve(root,'tests/fixtures/select-shell-tab.ps1'),'-ProcessId',String(browser.pid),'-Index',String(index)],{windowsHide:true});
  await select(1);await driver('popup-windows.ps1',['-Action','foreground']);
  await act('Choose a number','click');await sleep(300);
  const foregroundSelect=await driver('popup-windows.ps1');console.log(JSON.stringify({foregroundSelect}));
  assert(foregroundSelect.foregroundOwn,'Synthetic fixture lost foreground before the select check');
  assert(foregroundSelect.windows.some(w=>w.startsWith('Chrome_WidgetWin_1')),'Foreground select popup must remain available');
  checks.push({foregroundSelectVisible:true});
  await select(0);await sleep(300);
  let state=await driver('popup-windows.ps1');
  assert(!state.windows.some(w=>w.startsWith('Chrome_WidgetWin_1')),'Tab switch must hide an existing popup');
  assert.equal(state.capture,'','Tab switch must release popup capture');
  checks.push({tabSwitchDismissed:true});
  await select(1);await driver('popup-windows.ps1',['-Action','foreground']);
  await act('Choose a number','click');await sleep(300);
  assert((await driver('popup-windows.ps1')).windows.some(w=>w.startsWith('Chrome_WidgetWin_1')),'Reopening foreground select must work');
  await driver('popup-windows.ps1',['-Action','minimize']);await sleep(300);
  state=await driver('popup-windows.ps1');
  assert(!state.windows.some(w=>w.startsWith('Chrome_WidgetWin_1')),'Minimizing must hide an existing popup');
  assert.equal(state.capture,'','Minimizing must release popup capture');
  checks.push({minimizedDismissed:true});
  await driver('popup-windows.ps1',['-Action','foreground']);
  await sleep(500);
  await act('Open suggestions','click');await sleep(500);
  state=await driver('popup-windows.ps1');
  console.log(JSON.stringify({foregroundDatalist:state,pickerErrors:events.filter(e=>e.type==='picker-error')}));
  assert(state.foregroundOwn,'Synthetic fixture lost foreground before the datalist check');
  assert(state.windows.some(w=>w.startsWith('Chrome_WidgetWin_1')),'Foreground datalist suggestions must remain available');
  checks.push({foregroundDatalistVisible:true});
  await select(0);await sleep(300);
  state=await driver('popup-windows.ps1');
  assert(!state.windows.some(w=>w.startsWith('Chrome_WidgetWin_1')),'Tab switch must hide datalist suggestions');
  checks.push({datalistSwitchDismissed:true});
  }
}catch(e){failure=e.message;console.error(e);process.exitCode=1;}
finally{
  await client?.close().catch(()=>{});
  if(browser?.pid&&browser.exitCode===null&&browser.signalCode===null)await runFile('taskkill.exe',['/PID',String(browser.pid),'/T','/F'],{windowsHide:true}).catch(()=>{});
  server?.close();endHashes=await hashes().catch(()=>undefined);
  if(JSON.stringify(startHashes)!==JSON.stringify(endHashes)){failure='Binaries changed during test';process.exitCode=1;}
  await mkdir(resolve(root,'out'),{recursive:true});
  await writeFile(resolve(root,'out',`${run}.json`),JSON.stringify({run,startHashes,endHashes,checks,failure,passed:!failure,coverage:'Synthetic production-browser MCP actions and native window enumeration. No real accounts or physical mouse input.'},null,2));
  console.log(`Report: out/${run}.json`);
}
