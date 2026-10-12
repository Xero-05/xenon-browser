// Production browser and MCP, disposable profile, synthetic page transitions.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {randomBytes,randomUUID,createHash} from 'node:crypto';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {Client} from '@modelcontextprotocol/client';
import {StdioClientTransport} from '@modelcontextprotocol/client/stdio';
import {writePrivateConfig} from '../adapter/dist/src/private-config.js';
const root=resolve(import.meta.dirname,'..'),run=`filter-matrix-${Date.now()}-${randomBytes(4).toString('hex')}`,profile=resolve(root,'.cache',run),pipeName=`xenon-test-${run}`,pipe=`\\\\.\\pipe\\${pipeName}`;
const cases=[
 ['plain','','',true],['identity-clip','clip-path:inset(0px)','',true],['pixel-clip','clip-path:inset(10px)','',true],['percent-clip','clip-path:inset(1%)','',true],['negative-clip','clip-path:inset(-10px 0px -20px)','',true],
 ['distant-blur','','filter:blur(120px)',true],['zero-blur','','filter:blur(0px)',true],['distant-backdrop','','backdrop-filter:blur(100px)',true],['bounded-shadow','','box-shadow:0px 2px 6px rgba(0,0,0,.3)',true],
 ['rotated-blur','','filter:blur(30px);transform:rotate(45deg)',true],['scaled-blur','','filter:blur(30px);transform:scale(1.05)',true],
 ['distant-invert','','filter:invert(1)',true],['covered-by-invert','','left:20px;top:20px;width:200px;height:60px;filter:invert(1);z-index:2',false],
 ['backdrop-content','','',true],['translation','transform:translateX(10px)','',true],['nested-inset','clip-path:inset(0px)','',true],
 ['covered-by-blur','','filter:blur(600px)',false],['clipped-center','clip-path:inset(100px)','',false],['zero-area-clip','clip-path:inset(50%)','',false],['unsupported-round','clip-path:inset(0px round 20px)','',false],['unsupported-polygon','clip-path:polygon(0 0,100% 0,0 100%)','',false],
 ['covered-by-backdrop','','left:20px;top:20px;width:200px;height:60px;backdrop-filter:blur(10px);z-index:2',false],
 ['faint-control','opacity:0.1','',false],
];
const html=([name,parent,decoration])=>`<!doctype html><meta charset="utf-8"><title>Filter matrix ${name}</title><style>body{margin:0;background:white;color:#142237;font:16px system-ui}.container{width:800px;height:700px;${parent}}button{position:absolute;left:30px;top:30px;width:180px;height:36px;font:16px system-ui}.decoration{position:absolute;left:500px;top:900px;width:20px;height:20px;background:#fff;${decoration}}</style><div class="container"><div style="${name==='nested-inset'?'clip-path:inset(0px);height:700px':''}"><button id="control" aria-label="HIDDEN_MATRIX_ARIA_CANARY" onclick="document.querySelector('#count').textContent='Clicks: 1';fetch('/clicked')">Matrix action</button><p id="count" style="position:absolute;left:30px;top:90px">Clicks: 0</p></div><span style="display:none">HIDDEN_MATRIX_CANARY</span><input value="HIDDEN_MATRIX_EDIT_CANARY" style="position:absolute;left:30px;top:150px"></div><div class="decoration"></div><div style="height:1200px"></div><button style="position:absolute;top:1550px">Scrolled matrix action</button><div style="height:1200px"></div>`;
const exec=promisify(execFile),hash=async()=>createHash('sha256').update(await readFile(resolve(root,'build/app/Release/Xenon.dll'))).digest('hex'),results=[];
const collectClicks=process.argv.includes('--collect-uncertain-clicks'),logFile=resolve(profile,'visibility-diagnostics/uncertain-clicks.jsonl');
async function collection(action){const r=await exec('powershell.exe',['-NoProfile','-File',resolve(root,'tests/developer-elements-driver.ps1'),'-BrowserPid',String(browser.pid),'-Action',action],{windowsHide:true});return JSON.parse(r.stdout.trim());}
async function logRows(){try{return (await readFile(logFile,'utf8')).trim().split('\n').filter(Boolean).map(JSON.parse)}catch(e){if(e.code==='ENOENT')return [];throw e;}}
let browser,client,server,tab,initialHash,recordedClicks=0;
async function until(fn){const end=Date.now()+20000;while(Date.now()<end){try{const r=await fn();if(r)return r}catch{}await new Promise(r=>setTimeout(r,150))}throw Error('Matrix readiness timeout')}
async function raw(name,args){return client.callTool({name:'xenon_'+name,arguments:args})}
async function tool(name,args){const r=await raw(name,args);assert(!r.isError,JSON.stringify(r.structuredContent));return r.structuredContent}
const scope=()=>({agentSessionId:tab.agentSessionId,workspaceId:tab.workspaceId,tabId:tab.tabId}),mutation=()=>({...scope(),ownershipGeneration:tab.ownershipGeneration,operationId:randomUUID()});
try{
 await mkdir(profile,{recursive:true});await mkdir(resolve(root,'out'),{recursive:true});
 await writeFile(resolve(profile,'SYNTHETIC_TEST_PROFILE'),'FILTER_MATRIX_SYNTHETIC');
 await writeFile(resolve(profile,'ui-settings.json'),JSON.stringify({version:1,language:'en-US',theme:'dark',introductionCompleted:true,automaticUpdateChecks:false}));
 const identity={clientId:'matrix_'+randomBytes(8).toString('hex'),token:randomBytes(32).toString('hex'),pipe};
 await writeFile(resolve(profile,'broker-state.json'),JSON.stringify({version:1,clients:[{id:identity.clientId,name:'Filter matrix',tokenHash:createHash('sha256').update(identity.token).digest('hex')}],workspaces:[{id:'native-default',clients:[identity.clientId]}],accountGrants:[],operations:[]}));
 const config=resolve(profile,'client-config.json');await writePrivateConfig(config,identity);
 server=http.createServer((req,res)=>{if(req.url==='/clicked'){recordedClicks++;res.writeHead(204);res.end();return;}const variant=cases.find(c=>req.url==='/'+c[0])??cases[0];res.writeHead(200,{'Content-Type':'text/html','Cache-Control':'no-store'});res.end(variant[0]==='backdrop-content'?html(variant).replace('<button id="control"','<div style="position:absolute;left:20px;top:20px;width:220px;height:80px;backdrop-filter:blur(10px)"><button id="control"').replace('</button>','</button></div>'):html(variant))});await new Promise(r=>server.listen(0,'127.0.0.1',r));const base=`http://127.0.0.1:${server.address().port}`;
 initialHash=await hash();browser=spawn(resolve(root,'build/app/Release/Xenon.exe'),[`--user-data-dir=${profile}`,`--broker-pipe=${pipeName}`],{windowsHide:true,stdio:'ignore'});
 await until(()=>new Promise(r=>{const s=net.connect(pipe);s.once('connect',()=>{s.destroy();r(true)});s.once('error',()=>r(false))}));
 client=new Client({name:'Filter matrix',version:'1'},{versionNegotiation:{mode:{pin:'2026-07-28'}}});await client.connect(new StdioClientTransport({command:process.execPath,args:[resolve(root,'adapter/dist/src/cli.js'),'serve','--config',config],stderr:'pipe'}));
 const worker=await tool('worker_create',{name:'Filter matrix',workspaceId:'native-default'});tab={...worker,...await tool('tab_create',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,url:base+'/plain'})};
 if(collectClicks){const state=await collection('collection-state');assert.equal(state.toggleText,'Collect uncertain agent clicks');assert.deepEqual(await logRows(),[]);}
 let previous;
 for(const variant of cases){const [name,,,visible]=variant;try{
  await tool('navigate',{...mutation(),action:'navigate',url:base+'/'+name});
  const observed=await until(async()=>{const o=await tool('observe',{...scope(),maxNodes:1000});return o.url===base+'/'+name&&!o.loading&&o.viewport.width>0&&o});
  assert(!JSON.stringify(observed).includes('HIDDEN_MATRIX_'));
  const control=observed.nodes.find(n=>n.role==='button'&&(name==='backdrop-content'?n.name===''&&n.bounds.y<100:n.name==='Matrix action'));
  if(visible){assert(control?.ref,'Visible control missing');assert(observed.nodes.some(n=>n.name==='Clicks: 0'));
   if(previous){const stale=await raw('interact',{...mutation(),action:'click',observationId:previous.observationId,elementRef:previous.ref});assert(stale.isError,'Previous document reference accepted')}
   await tool('interact',{...mutation(),action:'click',observationId:observed.observationId,elementRef:control.ref});
   await until(async()=>{const o=await tool('observe',{...scope(),maxNodes:1000});return o.nodes.some(n=>n.name==='Clicks: 1')});
   previous={observationId:observed.observationId,ref:control.ref};
  }else{assert(!control,'Covered or unsupported control admitted');assert.equal(observed.coverage,'partial');
   if(!['clipped-center','zero-area-clip'].includes(name)){
    const candidate=observed.nodes.find(n=>n.role==='button'&&n.visibilityStatus==='uncertain'&&n.bounds.y<100);
    assert(candidate,'Uncertain control candidate missing');assert.equal(candidate.name,'');assert(!candidate.ref);assert.equal(candidate.screenshotRequired,true);assert(candidate.visibilityReason);
    const denied=await raw('interact',{...mutation(),action:'click',observationId:observed.observationId,elementRef:candidate.candidateId});assert(denied.isError,'Uncertain candidate became an action reference');
    if(name==='unsupported-round'){
     const inspected=await raw('inspect',{...scope(),maxNodes:1000});assert(!inspected.isError);assert(inspected.content.some(c=>c.type==='image'));
     const shot=inspected.structuredContent,visual=shot.nodes.find(n=>n.candidateId===candidate.candidateId);assert(visual?.screenshotRequired);assert(!visual.ref);
     const before=recordedClicks;await tool('interact',{...mutation(),action:'click',observationId:shot.observationId,x:120,y:48});
     await until(()=>recordedClicks>before);
     if(collectClicks){
      assert.deepEqual(await logRows(),[],'Default-off collection wrote a log');
      await collection('collection-toggle');await until(async()=>((await collection('collection-state')).toggleText==='Stop collecting uncertain clicks'));
      const fresh=await tool('inspect',{...scope(),maxNodes:1000});const clicks=recordedClicks;
      await tool('interact',{...mutation(),action:'click',observationId:fresh.observationId,x:120,y:48});await until(()=>recordedClicks>clicks);
      const rows=await logRows();assert.equal(rows.length,1);assert.equal(rows[0].visibilityStatus,'uncertain');assert.equal(rows[0].role,'button');assert.equal(rows[0].hitMatch,'native_ax_ancestry');assert.equal(rows[0].outcome,'mouse_down_and_up_dispatched');assert.equal(rows[0].websiteSuccessVerified,false);assert.equal(rows[0].filterDiagnostics.nonrectangularClip,true);
      const hover=await tool('inspect',{...scope(),maxNodes:1000});await tool('interact',{...mutation(),action:'hover',observationId:hover.observationId,x:120,y:48});assert.equal((await logRows()).length,1,'Hover entered the click log');
      assert(!JSON.stringify(rows).includes('HIDDEN_MATRIX_'));assert(!JSON.stringify(rows).includes('candidate-'));assert(!JSON.stringify(rows).includes('127.0.0.1'));assert(!JSON.stringify(rows).includes(identity.token));
     }
    }
    if(collectClicks&&name==='covered-by-backdrop'){
     const shot=await tool('inspect',{...scope(),maxNodes:1000}),before=recordedClicks;
     await tool('interact',{...mutation(),action:'click',observationId:shot.observationId,x:120,y:48});assert.equal(recordedClicks,before);assert.equal((await logRows()).length,1,'A covered control was falsely logged as clicked');
    }
   }
  }
  results.push({name,passed:true});console.log('PASS '+name);
 }catch(e){try{await writeFile(resolve(profile,name+'-failure.json'),JSON.stringify(await tool('observe',{...scope(),maxNodes:1000})))}catch{}results.push({name,passed:false,error:e.message});console.log('FAIL '+name+': '+e.message)}}
 try{await tool('navigate',{...mutation(),action:'navigate',url:base+'/identity-clip'});let o=await until(async()=>{const n=await tool('observe',{...scope(),maxNodes:1000});return n.nodes.some(n=>n.name==='Matrix action')&&n});await tool('interact',{...mutation(),action:'scroll',direction:'down',amount:1400,observationId:o.observationId});o=await until(async()=>{const n=await tool('observe',{...scope(),maxNodes:1000});return n.viewport.pageY>500&&n.nodes.some(n=>n.name==='Scrolled matrix action')&&n});assert(!JSON.stringify(o).includes('HIDDEN_MATRIX_'));results.push({name:'Scroll after navigation',passed:true});console.log('PASS Scroll after navigation')}catch(e){results.push({name:'Scroll after navigation',passed:false,error:e.message});console.log('FAIL Scroll after navigation: '+e.message)}
 if(collectClicks){try{
  assert.equal((await logRows()).length,1);
  await tool('navigate',{...mutation(),action:'navigate',url:base+'/plain'});await until(async()=>{const o=await tool('observe',{...scope(),maxNodes:1000});return !o.loading&&o.url===base+'/plain'&&o});
  const verified=await tool('inspect',{...scope(),maxNodes:1000});await tool('interact',{...mutation(),action:'click',observationId:verified.observationId,x:120,y:48});assert.equal((await logRows()).length,1,'Verified coordinate click entered uncertain log');
  await collection('collection-toggle');await until(async()=>((await collection('collection-state')).toggleText==='Collect uncertain agent clicks'));
  await tool('navigate',{...mutation(),action:'navigate',url:base+'/unsupported-round'});await until(async()=>{const o=await tool('observe',{...scope(),maxNodes:1000});return !o.loading&&o.url===base+'/unsupported-round'&&o});
  const shot=await tool('inspect',{...scope(),maxNodes:1000});await tool('interact',{...mutation(),action:'click',observationId:shot.observationId,x:120,y:48});assert.equal((await logRows()).length,1,'Disabled collection wrote another record');
  const stale=await raw('interact',{...mutation(),action:'click',observationId:verified.observationId,x:120,y:48});assert(stale.isError);assert.equal((await logRows()).length,1);
  await collection('collection-clear');assert.deepEqual(await logRows(),[]);assert.equal((await collection('collection-state')).toggleText,'Collect uncertain agent clicks');
  results.push({name:'Opt-in click collection, native hit identity, redaction, disable and clear',passed:true});console.log('PASS Opt-in click collection');
 }catch(e){results.push({name:'Opt-in click collection',passed:false,error:e.message});console.log('FAIL Opt-in click collection: '+e.message)}}
}catch(e){results.push({name:'Harness',passed:false,error:e.message});console.log('FAIL Harness: '+e.message)}
finally{if(client)await client.close().catch(()=>{});if(browser?.pid&&browser.exitCode===null)await exec('taskkill.exe',['/PID',String(browser.pid),'/T','/F'],{windowsHide:true}).catch(()=>{});server?.close();const finalHash=await hash(),passed=results.length===cases.length+1+(collectClicks?1:0)&&results.every(r=>r.passed)&&initialHash===finalHash;const path=resolve(root,'out',run+'.json');await writeFile(path,JSON.stringify({run,applicationDllSha256:initialHash,applicationDllSha256AtEnd:finalHash,collectionTest:collectClicks,results,passed},null,2));console.log('Report: '+path);process.exitCode=passed?0:1}
