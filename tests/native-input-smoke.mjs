// Interactive Windows input regression. When prompted, edit the draft in the
// "Xenon Windows input fixture" window. Only generated local state is used.
// Optional --dialog adds a physical confirmation/dismissal check; the harness
// does not open, hide, click or answer any native dialog itself.
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
const root=resolve(import.meta.dirname,'..'),run=`native-input-${Date.now()}`;
const dialogMode=process.argv.includes('--dialog'),fixtureTitle='Xenon Windows input fixture';
const pipeName=`xenon-test-${run}`,pipePath=`\\\\.\\pipe\\${pipeName}`;
const profile=resolve(root,'.cache',run),results=[];
const hash=async()=>createHash('sha256').update(await readFile(resolve(root,'build/app/Release/Xenon.dll'))).digest('hex');
const applicationDllSha256=await hash(),runFile=promisify(execFile);
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const pipeAlive=()=>new Promise(r=>{const s=net.connect(pipePath);s.once('connect',()=>{s.destroy();r(true)});s.once('error',()=>{s.destroy();r(false)})});
async function until(fn,ms=15000){const end=Date.now()+ms;while(Date.now()<end){const r=await fn();if(r)return r;await sleep(100)}throw new Error('Condition timed out')}
async function check(name,fn){const start=Date.now();try{const details=await fn();results.push({name,passed:true,elapsedMs:Date.now()-start,...(details?{details}:{})});console.log(`PASS ${name}`)}catch(e){results.push({name,passed:false,error:e.message});throw e}}
if(await pipeAlive())throw new Error('The generated input test pipe is already in use. Start a fresh test run.');
await mkdir(profile,{recursive:true});
const config={clientId:'native_input_fixture',token:randomBytes(32).toString('hex'),pipe:pipePath};
await writePrivateConfig(resolve(profile,'client-config.json'),config);
await writeFile(resolve(profile,'broker-state.json'),JSON.stringify({version:1,clients:[{id:config.clientId,name:'Native input fixture',tokenHash:createHash('sha256').update(config.token).digest('hex')}],workspaces:[],accountGrants:[],operations:[]}));
let dialogTelemetry={opened:0,accepted:0,dismissed:0},humanEdited=false;
const fixtureHtml=`<!doctype html><title>${fixtureTitle}</title><style>body{background:#fff;color:#111;font:18px sans-serif;margin:30px}label{display:block;background:#fff;padding:8px}input{margin-left:8px}button{font:inherit;padding:12px}</style><h1>Ready for Windows input</h1><label>Draft <input aria-label="Draft"></label><p>Append human edit to the draft when the test requests it.</p><p id="draftPreview"></p><script>document.querySelector('input').addEventListener('input',e=>{if(e.isTrusted){document.querySelector('#draftPreview').textContent='Draft now: '+e.target.value;if(e.target.value==='Unsubmitted draft human edit')fetch('/human-edited',{method:'POST'})}})</script>${dialogMode?`<button id="confirmLogout">Confirm synthetic logout</button><p id="dialogResult">Synthetic logout untouched</p><script>
const telemetry={opened:0,accepted:0,dismissed:0};
const report=()=>fetch('/dialog-telemetry',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(telemetry)}).catch(()=>{});
document.querySelector('#confirmLogout').onclick=()=>{telemetry.opened++;report();const accepted=confirm('Sign out of the synthetic input fixture?');if(accepted)telemetry.accepted++;else telemetry.dismissed++;document.querySelector('#dialogResult').textContent=accepted?'Synthetic logout confirmed':'Synthetic logout cancelled';report();};
</script>`:''}`;
const server=http.createServer((req,res)=>{
  res.setHeader('Cache-Control','no-store');
  if(req.url==='/human-edited'&&req.method==='POST'){humanEdited=true;res.end('ok');return;}
  if(req.url==='/dialog-telemetry'&&req.method==='POST'){
    let body='';req.on('data',part=>{body+=part;if(body.length>2048)req.destroy()});
    req.on('end',()=>{try{const value=JSON.parse(body);for(const key of ['opened','accepted','dismissed'])assert(Number.isSafeInteger(value[key])&&value[key]>=0&&value[key]<=100);if(value.opened>=dialogTelemetry.opened&&value.accepted+value.dismissed>=dialogTelemetry.accepted+dialogTelemetry.dismissed)dialogTelemetry={opened:value.opened,accepted:value.accepted,dismissed:value.dismissed};res.end('ok')}catch{res.writeHead(400).end()}});return;
  }
  res.setHeader('Content-Type','text/html; charset=utf-8');res.end(fixtureHtml);
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const browser=spawn(resolve(root,'build/app/Release/Xenon.exe'),[`--user-data-dir=${profile}`,`--broker-pipe=${pipeName}`],{windowsHide:true,stdio:'ignore'});
let client;
try{
  await until(pipeAlive,30000);
  client=new Client({name:'Xenon native input test',version:'1'});
  await client.connect(new StdioClientTransport({command:process.execPath,args:[resolve(root,'adapter/dist/src/cli.js'),'serve','--config',resolve(profile,'client-config.json')],stderr:'pipe'}));
  const raw=(name,args={})=>client.callTool({name:`xenon_${name}`,arguments:args});
  const tool=async(name,args={})=>{const r=await raw(name,args);assert(!r.isError,JSON.stringify(r.structuredContent));return r.structuredContent};
  const observe=async args=>{
    const deadline=Date.now()+5000;
    while(true){
      const r=await raw('observe',args);
      // Retry only the documented transient read, never a mutation.
      if(r.isError&&r.structuredContent?.error?.code==='privacy_guard_initializing'&&Date.now()<deadline){await sleep(100);continue}
      assert(!r.isError,JSON.stringify(r.structuredContent));return r.structuredContent;
    }
  };
  const worker=await tool('worker_create',{name:'Windows input owner'});
  const tab={...worker,...await tool('tab_create',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,url:`http://127.0.0.1:${server.address().port}/`})};
  const scope={agentSessionId:tab.agentSessionId,workspaceId:tab.workspaceId,tabId:tab.tabId};
  let evidence=await until(async()=>{const r=await observe(scope);return r.nodes?.some(n=>n.name==='Ready for Windows input')?r:false});
  const draft=evidence.nodes.find(n=>n.role==='textbox'&&n.name==='Draft');assert(draft,'Expected a rendered label for the fixture draft input');
  assert.equal(tab.ownerSessionId,worker.agentSessionId,'The creator must automatically own its new tab');
  await tool('interact',{...scope,ownershipGeneration:tab.ownershipGeneration,operationId:randomUUID(),observationId:evidence.observationId,action:'fill',elementRef:draft.ref,text:'Unsubmitted draft'});
  evidence=await observe(scope);
  const beforeInput=await tool('control_status',scope);
  console.log('READY FOR WINDOWS INPUT: Select the Xenon Windows input fixture in the workspace sidebar, then click Draft. Waiting up to 10 minutes for the native UI check.');
  await check('Windows page input pauses dispatch while retaining creator ownership',async()=>{
    const state=await until(async()=>{const s=await tool('control_status',scope);return s.humanPaused?s:false},600000);
    assert.equal(state.ownerSessionId,worker.agentSessionId);
    assert.equal(state.ownershipGeneration,tab.ownershipGeneration);
    assert(state.humanActivityEpoch>beforeInput.humanActivityEpoch);
    const denied=await raw('interact',{...scope,ownershipGeneration:tab.ownershipGeneration,operationId:randomUUID(),observationId:evidence.observationId,action:'key',key:'ArrowLeft'});
    assert(denied.isError);assert.equal(denied.structuredContent.error.code,'HUMAN_INPUT_PAUSED');
    const pausedRead=await raw('observe',scope);assert(pausedRead.isError);assert.equal(pausedRead.structuredContent.error.code,'HUMAN_INPUT_PAUSED');
  });
  await check('Idle resumes authority only with fresh evidence and preserves the human draft',async()=>{
    const beforeKeys=await until(async()=>{const s=await tool('control_status',scope);return !s.humanPaused?s:false},30000);
    console.log('READY FOR KEYBOARD EDIT: The click pause ended. Append " human edit" to the focused Draft now.');
    await until(()=>humanEdited,600000);
    const afterKeys=await tool('control_status',scope);
    assert(afterKeys.humanActivityEpoch>beforeKeys.humanActivityEpoch,'Native page keyboard input must report fresh activity separately from the click');
    assert.equal(afterKeys.humanPaused,true,'Keyboard input must establish its own pause');
    const state=await until(async()=>{const s=await tool('control_status',scope);return !s.humanPaused?s:false},30000);
    assert.equal(state.ownerSessionId,worker.agentSessionId);assert.equal(state.ownershipGeneration,tab.ownershipGeneration);assert.equal(state.requiresFreshObservation,true);
    const denied=await raw('interact',{...scope,ownershipGeneration:tab.ownershipGeneration,operationId:randomUUID(),observationId:evidence.observationId,action:'key',key:'ArrowLeft'});
    assert(denied.isError);assert.equal(denied.structuredContent.error.code,'OBSERVATION_REQUIRED');
    const after=await observe(scope);assert.equal(after.documentId,evidence.documentId);
    assert(after.nodes.some(n=>n.name?.includes('Draft now: Unsubmitted draft human edit')),'The human edit remains on the original document');
    await tool('interact',{...scope,ownershipGeneration:tab.ownershipGeneration,operationId:randomUUID(),observationId:after.observationId,action:'key',key:'ArrowLeft'});
  });
  if(dialogMode){
    console.log(`READY FOR HUMAN DIALOG: ${JSON.stringify({title:fixtureTitle,pid:browser.pid,tabId:tab.tabId,workspaceId:tab.workspaceId})}`);
    console.log('Hide only this fixture instance’s Xenon Controls, click Confirm synthetic logout in the fixture page, verify Controls appears with this exact tab selected, then click Dismiss. Waiting up to180seconds.');
    await check('Human dialog holds the pause until dismissal without taking ownership',async()=>{
      await until(()=>dialogTelemetry.opened>0,180000);
      await sleep(2200);
      const paused=await tool('control_status',scope);assert.equal(paused.humanPaused,true,'A human dialog must pause beyond the ordinary idle period');
      assert.equal(paused.ownerSessionId,worker.agentSessionId);assert.equal(paused.ownershipGeneration,tab.ownershipGeneration);
      const result=await until(()=>dialogTelemetry.accepted+dialogTelemetry.dismissed>0?{...dialogTelemetry}:false,180000);
      assert.equal(result.opened,1,'Exactly one physical fixture confirmation must be opened');
      assert.equal(result.accepted,0,'The UI check requests Dismiss, not Accept dialog');
      assert.equal(result.dismissed,1,'The website must receive one native dismissal');
      const state=await until(async()=>{const current=await tool('control_status',scope);return !current.inputBusy?current:false});
      assert.equal(state.ownerSessionId,worker.agentSessionId,'A native dialog response must preserve agent ownership');
      assert.equal(state.ownershipGeneration,tab.ownershipGeneration);
      const after=await observe(scope);assert.equal(after.documentId,evidence.documentId);
      assert(after.nodes.some(node=>node.name==='Synthetic logout cancelled'),'The original live page must display the dismissal result');
      return {...result,documentPreserved:true,ownerPreserved:true,uiSurfaceVerification:'manual observation required; response verified through fixture telemetry'};
    });
  }
}catch(e){if(!results.some(r=>!r.passed))results.push({name:'Native input harness',passed:false,error:e.message});console.log(`FAIL ${e.message}`)}
finally{
  await client?.close().catch(()=>{});
  if(browser.pid)await runFile('taskkill.exe',['/PID',String(browser.pid),'/T','/F'],{windowsHide:true}).catch(()=>{});
  server.closeAllConnections();await new Promise(r=>server.close(r));
  const applicationDllSha256AtEnd=await hash();
  if(applicationDllSha256AtEnd!==applicationDllSha256)results.push({name:'Unchanged test binary',passed:false});
  const report={run,capturedAt:new Date().toISOString(),binary:'build/app/Release/Xenon.exe',profile,applicationDllSha256,applicationDllSha256AtEnd,dialogUiMode:dialogMode,passed:results.length===(dialogMode?3:2)&&results.every(r=>r.passed),results};
  await writeFile(resolve(root,'out/native-input-smoke-results.json'),JSON.stringify(report,null,2));
  process.exitCode=report.passed?0:1;
}
