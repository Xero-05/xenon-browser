// Production CEF/MCP plus native window and keyboard messages, all on a
// disposable profile and loopback fixture. Does not claim physical UI coverage.
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
const root=resolve(import.meta.dirname,'..'),run=`developer-elements-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile=resolve(root,'.cache',run),pipeName=`xenon-test-${run}`,pipe=`\\\\.\\pipe\\${pipeName}`;
const dll=resolve(root,'build/app/Release/Xenon.dll'),hash=async()=>createHash('sha256').update(await readFile(dll)).digest('hex');
const runFile=promisify(execFile),sleep=ms=>new Promise(r=>setTimeout(r,ms)),results=[];
// Selection painting and UI-state messages do not need foreground/capture.
// Keep the full suite's physical-window prerequisites for divider/hover checks.
const selectionOnly=process.argv.includes('--selection-only');
let browser,client,server,tab,initialHash;
const scope=()=>({agentSessionId:tab.agentSessionId,workspaceId:tab.workspaceId,tabId:tab.tabId});
const mutate=()=>({...scope(),ownershipGeneration:tab.ownershipGeneration,operationId:randomUUID()});
async function raw(name,args={}){return client.callTool({name:`xenon_${name}`,arguments:args});}
async function tool(name,args={}){const r=await raw(name,args);assert(!r.isError,JSON.stringify(r.structuredContent));return r.structuredContent;}
async function until(fn,ms=20000){const end=Date.now()+ms;while(Date.now()<end){try{const r=await fn();if(r)return r;}catch{}await sleep(200);}throw new Error('Developer elements fixture timed out');}
async function driver(action='state',name,image){const {stdout}=await runFile('powershell.exe',['-NoProfile','-NonInteractive','-File',resolve(root,'tests/developer-elements-driver.ps1'),'-BrowserPid',String(browser.pid),'-Action',action,...(name?['-Name',name]:[]),...(image?['-Image',resolve(profile,image)]:[])],{windowsHide:true,maxBuffer:4*1024*1024});return JSON.parse(stdout);}
async function check(name,fn){if(selectionOnly&&!name.startsWith('Menu enables')&&!name.startsWith('Element list selection'))return;const start=Date.now();try{await fn();results.push({name,passed:true,elapsedMs:Date.now()-start});console.log(`PASS ${name}`);}catch(e){results.push({name,passed:false,error:e.message});console.log(`FAIL ${name}: ${e.message}`);}}
const html=`<!doctype html><meta charset="utf-8"><title>Developer elements fixture</title>
<style>body{font:16px system-ui;background:white;color:#182433;margin:16px}button,input,select{margin:8px 0;padding:8px;display:block}.hidden{display:none}</style>
<h1>Rendered fixture heading</h1><div style="transform:translateX(-11px);margin-left:32px"><div style="transform:scaleY(-1)"><a id="result" href="#result" onclick="event.preventDefault();this.nextElementSibling.textContent='Result clicks: 1'"><span style="display:block;font-size:10px;transform:scaleY(-1)">Result site</span><span style="display:block;transform:scaleY(-1)">Translated result title</span></a><span id="result-count" style="display:block;transform:scaleY(-1)">Result clicks: 0</span></div></div><button id="action"><span style="display:block;padding:4px">Visible action</span></button><p id="count">Count: 0</p>
<style>#icon{position:relative}#icon::after{content:'';position:absolute;inset:0}</style><button id="icon" aria-label="Site apps"><svg width="24" height="24" viewBox="0 0 24 24"><path d="M0 0H24V24H0Z"/></svg></button>
<input type="submit" value="Rendered submit label" aria-label="HIDDEN_SUBMIT_ARIA_CANARY">
<textarea role="combobox" aria-label="HIDDEN_SEARCH_ARIA_CANARY" style="height:24px">SYNTHETIC_SEARCH_VALUE</textarea>
<label for="field">Visible field</label><input id="field" value="SYNTHETIC_EDIT_VALUE">
<button disabled>Disabled control</button><label><input type="checkbox">Visible checkbox</label>
<button id="protect">Protect fixture login</button><input id="password" type="password" autocomplete="current-password">
<div class="hidden">HIDDEN_PANEL_CANARY</div><button aria-label="HIDDEN_ARIA_CANARY">Rendered button label</button>
<button style="position:absolute;left:300px;top:500px;width:140px;height:36px;font-size:8px">HIDDEN_BACKDROP_CANARY</button><div style="position:absolute;left:300px;top:500px;width:140px;height:36px;backdrop-filter:blur(10px);z-index:2;pointer-events:none"></div>
<div style="height:1400px"></div><button>Other scrolled control</button><button id="scrolled">Scrolled control</button><div style="height:1200px"></div><div style="position:absolute;left:500px;top:6000px;width:20px;height:20px;backdrop-filter:blur(100px)"></div>
<button style="position:absolute;left:300px;top:1850px;width:140px;height:36px;font-size:8px">HIDDEN_SCROLLED_UNCERTAIN</button><div style="position:absolute;left:300px;top:1850px;width:140px;height:36px;backdrop-filter:blur(10px);z-index:2;pointer-events:none"></div>
<script>let clicks=0;action.onclick=()=>{count.textContent='Count: '+(++clicks)};
protect.onclick=()=>{password.value='SYNTHETIC_PASSWORD_CANARY';password.dispatchEvent(new Event('input',{bubbles:true}))};</script>`;
try{
  if(process.platform!=='win32')throw new Error('Windows required');
  await mkdir(profile,{recursive:true});await mkdir(resolve(root,'out'),{recursive:true});
  await writeFile(resolve(profile,'SYNTHETIC_TEST_PROFILE'),'Developer elements synthetic fixture');
  await writeFile(resolve(profile,'ui-settings.json'),JSON.stringify({version:1,theme:process.env.XENON_TEST_THEME??'dark',language:process.env.XENON_TEST_LANGUAGE??'en-US',introductionCompleted:true,automaticUpdateChecks:false}));
  const identity={clientId:`fixture_${randomBytes(8).toString('hex')}`,token:randomBytes(32).toString('hex'),pipe};
  await writeFile(resolve(profile,'broker-state.json'),JSON.stringify({version:1,clients:[{id:identity.clientId,name:'Developer elements fixture',tokenHash:createHash('sha256').update(identity.token).digest('hex')}],workspaces:[{id:'native-default',clients:[identity.clientId]}],accountGrants:[],operations:[]}));
  const config=resolve(profile,'client-config.json');await writePrivateConfig(config,identity);
  server=http.createServer((req,res)=>{res.writeHead(200,{'Content-Type':'text/html; charset=utf-8'});res.end(html.replace('Developer elements fixture',req.url==='/second'?'Second elements fixture':'Developer elements fixture').replace('Rendered fixture heading',req.url==='/second'?'Second rendered heading':'Rendered fixture heading'));});
  await new Promise(r=>server.listen(0,'127.0.0.1',r));const base=`http://127.0.0.1:${server.address().port}`;initialHash=await hash();
  browser=spawn(resolve(root,'build/app/Release/Xenon.exe'),[`--user-data-dir=${profile}`,`--broker-pipe=${pipeName}`],{windowsHide:true,stdio:'ignore'});
  await until(()=>new Promise(r=>{const s=net.connect(pipe);s.once('connect',()=>{s.destroy();r(true)});s.once('error',()=>r(false))}));
  if(!selectionOnly)await driver('prepare');
  client=new Client({name:'Elements fixture',version:'1'},{versionNegotiation:{mode:{pin:'2026-07-28'}}});
  await client.connect(new StdioClientTransport({command:process.execPath,args:[resolve(root,'adapter/dist/src/cli.js'),'serve','--config',config],stderr:'pipe'}));
  const worker=await tool('worker_create',{name:'Elements fixture worker',workspaceId:'native-default'});
  tab={...worker,...await tool('tab_create',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,url:base})};
  await until(async()=>{const r=await raw('observe',{...scope(),maxNodes:1000});return !r.isError&&r.structuredContent.nodes.some(n=>n.name==='Visible action')});
  await check('Menu enables a website-side panel using filtered evidence',async()=>{
    const initial=await driver();assert.equal(initial.visible,false);assert.equal(initial.count,0);
    await driver('open');await driver('select-tab','Developer elements fixture');
    const state=await until(async()=>{const s=await driver();return s.visible&&s.labels.includes('Visible action')&&s.labels.includes('Rendered fixture heading')&&s});
    assert(!JSON.stringify(state).includes('HIDDEN_PANEL_CANARY'));assert(!JSON.stringify(state).includes('HIDDEN_ARIA_CANARY'));assert(!JSON.stringify(state).includes('SYNTHETIC_EDIT_VALUE'));
    await driver('state',undefined,'panel.png');
  });
  await check('Element list selection uses the theme and tab reclick hides only the keyboard focus cue',async()=>{
    await driver('select-tab','Developer elements fixture');
    const cues=await driver('tab-focus-cues');assert.equal(cues.keyboardBefore&1,0);assert.equal(cues.mouse&1,1);assert.equal(cues.keyboardAfter&1,0);assert(cues.sameSelectedTab);
    await driver('select-element','Visible action');
    const colors=await driver('selection-colors',(process.env.XENON_TEST_THEME??'dark')==='light'?'228,228,228':'65,65,65','selected-list.png');
    assert(colors.themedSelectionPixels>20);assert.equal(colors.systemHighlightPixels,0);
  });
  await check('Backdrop effects stay local and continue withholding covered controls',async()=>{
    const state=await driver();assert(state.labels.includes('Visible action'));assert(!state.labels.includes('HIDDEN_BACKDROP_CANARY'));
    const observed=await tool('observe',{...scope(),maxNodes:1000});assert(observed.nodes.some(n=>n.name==='Visible action'));
    assert(!JSON.stringify(observed).includes('HIDDEN_BACKDROP_CANARY'));assert(!JSON.stringify(observed).includes('untrustedUnsupportedPaint'));
  });
  await check('Uncertain controls remain listed with screenshot guidance and no usable target',async()=>{
    const state=await driver(),candidate=state.nodes.find(n=>n.role==='button'&&n.visibilityStatus==='uncertain');
    assert(candidate);assert(candidate.screenshotRequired);assert(!candidate.targetAvailable);assert(!candidate.siteLabel);assert(candidate.visibilityReason);
    await driver('select-uncertain');
    const details=await until(async()=>{const n=JSON.parse((await driver('selection-state')).details);return n.highlighted&&n.highlightVerification==='approximate_position_only'&&n});
    assert.equal(details.visibilityStatus,'uncertain');assert(!details.highlightError);assert(!details.targetAvailable);
    await driver('select-element','Visible action');await driver('page-hover',JSON.stringify({...candidate,viewport:state.nodes[0].viewport}));
    await until(async()=>{const n=JSON.parse((await driver('selection-state')).details);return n.diagnosticId===candidate.diagnosticId&&n.highlighted&&n.highlightVerification==='approximate_position_only'&&!n.hoverError&&n});
    await sleep(3500);const refreshed=JSON.parse((await driver('selection-state')).details);assert.equal(refreshed.visibilityStatus,'uncertain');assert(refreshed.highlighted);assert(!refreshed.highlightError);
    await driver('selection-state',undefined,'approximate-highlight.png');
    await driver('select-element','Visible action');
  });
  await check('Native submit controls and editable comboboxes preserve label boundaries',async()=>{
    const state=await driver(),submit=state.nodes.find(n=>n.tag==='INPUT'&&n.inputType==='submit'),search=state.nodes.find(n=>n.tag==='TEXTAREA'&&n.role==='combobox');
    assert.equal(submit?.role,'button');assert(submit?.targetAvailable);assert.equal(submit?.name,'');assert(search?.targetAvailable);
    assert(submit?.labelExplanation); // Native explanation distinguishes detection from a verified name.
    assert.equal(submit.siteLabel?.text,'HIDDEN_SUBMIT_ARIA_CANARY');assert.equal(submit.siteLabel?.renderedNameVerified,false);assert.equal(submit.siteLabel?.contentTrust,'untrusted_website_metadata');
    assert.equal(search.siteLabel?.text,'HIDDEN_SEARCH_ARIA_CANARY');assert(!JSON.stringify(state).includes('SYNTHETIC_SEARCH_VALUE'));
    const observed=await tool('observe',{...scope(),maxNodes:1000});assert(observed.nodes.some(n=>n.tag==='INPUT'&&n.inputType==='submit'&&n.role==='button'&&n.ref));
    assert(!JSON.stringify(observed).includes('HIDDEN_SUBMIT_ARIA_CANARY'));assert(!JSON.stringify(observed).includes('HIDDEN_SEARCH_ARIA_CANARY'));assert(!JSON.stringify(observed).includes('siteLabel'));
  });
  await check('Panel width and list/details divider resize independently with minimum bounds',async()=>{
    const before=await driver(),wider=await driver('resize-width','80');assert(wider.panelWidth>before.panelWidth+40);
    const split=await driver('resize-split','-60');assert(split.listHeight<wider.listHeight-30);assert(split.detailsHeight>wider.detailsHeight+30);assert.equal(split.panelWidth,wider.panelWidth);
    const narrow=await driver('resize-width','-5000');assert(narrow.panelWidth>=240);assert(narrow.panelWidth<wider.panelWidth);
    const minimum=await driver('resize-split','-5000');assert(minimum.listHeight>=80);assert(minimum.detailsHeight>45);
    const maximum=await driver('resize-split','5000');assert(maximum.detailsHeight>=45);assert(maximum.listHeight>minimum.listHeight);
    await driver('resize-width','160');await driver('resize-split','-200');
    await until(async()=>{const s=await driver();return s.labels.includes('Visible action')});
  });
  await check('Selection shows frame, bounds and states without agent action tokens',async()=>{
    const state=await driver('select-element','Visible action'),node=JSON.parse(state.details);
    assert.equal(node.name,'Visible action');assert.equal(node.role,'button');assert(node.bounds);assert(node.frameId);assert(!('ref' in node));assert.equal(node.targetAvailable,true);
  });
  await check('Selecting a row highlights its current rendered node',async()=>{
    await driver('select-element','Visible action');
    const first=await until(async()=>{const s=await driver('selection-state'),n=JSON.parse(s.details||'{}');return n.name==='Visible action'&&n.highlighted&&n});
    await sleep(2000);const next=JSON.parse((await driver('selection-state')).details);
    assert.equal(next.diagnosticId,first.diagnosticId);assert.equal(next.highlighted,true);
    assert(!next.highlightError);assert(!next.hoverError);
    await driver('selection-state',undefined,'highlight.png');
  });
  await check('Page picker selects the corresponding row without activating it',async()=>{
    const button=JSON.parse((await driver('select-element','Visible action')).details);
    await driver('select-element','Rendered fixture heading');await driver('pick');await sleep(500);
    await driver('page-pick',JSON.stringify(button));
    await until(async()=>{const s=await driver('selection-state'),n=JSON.parse(s.details||'{}');return n.name==='Visible action'&&n.highlighted});
    const observed=await tool('observe',{...scope(),maxNodes:1000});assert(observed.nodes.some(n=>n.name==='Count: 0'));
  });
  await check('Passive page hover matches a nested control without activating it',async()=>{
    const button=JSON.parse((await driver('select-element','Visible action')).details);
    await driver('select-element','Rendered fixture heading');await driver('page-hover',JSON.stringify(button));
    await until(async()=>{const s=await driver('selection-state'),n=JSON.parse(s.details||'{}');return n.name==='Visible action'&&n.highlighted});
    const observed=await tool('observe',{...scope(),maxNodes:1000});assert(observed.nodes.some(n=>n.name==='Count: 0'));
  });
  await check('Code scrolling survives repeated inspector refreshes',async()=>{
    await driver('select-element','Visible action');const scrolled=await driver('scroll-details');assert(scrolled.detailsFirstLine>0);
    await sleep(3500);const next=await driver('selection-state');assert.equal(next.detailsFirstLine,scrolled.detailsFirstLine);
  });
  await check('Translated split result link with canceling reflections is observed, hovered and clicked',async()=>{
    const state=await driver(),link=state.nodes.find(n=>n.role==='link'&&n.name==='Result site Translated result title');assert(link?.targetAvailable);
    await driver('select-element','Visible action');await driver('page-hover',JSON.stringify(link));
    await until(async()=>{const n=JSON.parse((await driver('selection-state')).details||'{}');return n.role==='link'&&n.name==='Result site Translated result title'&&n.highlighted});
    const observed=await tool('observe',{...scope(),maxNodes:1000}),target=observed.nodes.find(n=>n.role==='link'&&n.name==='Result site Translated result title');assert(target?.ref);
    assert(observed.nodes.some(n=>n.name==='Result clicks: 0'));
    await tool('interact',{...mutate(),action:'click',observationId:observed.observationId,elementRef:target.ref});
    await until(async()=>{const after=await tool('observe',{...scope(),maxNodes:1000});return after.nodes.some(n=>n.name==='Result clicks: 1')});
  });
  await check('Hovering an SVG icon resolves its listed button',async()=>{
    const icon=await until(async()=>{const state=await driver(),node=state.nodes.find(n=>n.siteLabel?.text==='Site apps');return node?.targetAvailable&&node});
    await driver('select-element','Visible action');await driver('page-hover',JSON.stringify(icon));
    await until(async()=>{const n=JSON.parse((await driver('selection-state')).details||'{}');return n.siteLabel?.text==='Site apps'&&n.highlighted});
  });
  await check('Canceling the picker restores ordinary selection',async()=>{
    await driver('pick');await sleep(300);await driver('pick');await sleep(300);
    await until(async()=>{const s=await driver();return s.labels.includes('Rendered fixture heading')});
    await driver('select-element','Rendered fixture heading');
    await until(async()=>{const s=await driver('selection-state'),n=JSON.parse(s.details||'{}');return n.name==='Rendered fixture heading'&&n.highlighted});
  });
  await check('Interactable filter removes text and disabled controls',async()=>{
    await until(async()=>{const s=await driver();return s.labels.includes('Visible action')});
    await driver('select-element','Visible action');
    const before=await driver();await driver('filter');const after=await driver();
    assert(after.count>0&&after.count<before.count);assert(!after.labels.includes('Rendered fixture heading'));assert(!after.labels.includes('Disabled control'));
    await driver('filter');
  });
  await check('Panel refresh leaves an existing agent observation usable',async()=>{
    const observed=await tool('observe',{...scope(),maxNodes:1000}),button=observed.nodes.find(n=>n.name==='Visible action'&&n.role==='button');assert(button?.ref);
    await sleep(3500);await driver('state');
    await tool('interact',{...mutate(),action:'click',observationId:observed.observationId,elementRef:button.ref});
    await until(async()=>{const s=await driver();return s.labels.includes('Count: 1')});
  });
  await check('Page scrolling keeps hover and highlight aligned with viewport coordinates',async()=>{
    let observed=await tool('observe',{...scope(),maxNodes:1000});
    await tool('interact',{...mutate(),action:'scroll',observationId:observed.observationId,direction:'down',amount:1600});
    observed=await until(async()=>{const o=await tool('observe',{...scope(),maxNodes:1000});return o.viewport.pageY>500&&o.nodes.some(n=>n.name==='Scrolled control')&&o});
    const button=await until(async()=>{const s=await driver(),n=s.nodes.find(n=>n.name==='Scrolled control');return n&&n});
    const uncertain=await until(async()=>{const s=await driver(),n=s.nodes.find(n=>n.role==='button'&&n.visibilityStatus==='uncertain'&&n.bounds.y>1500);return n&&n});
    await driver('page-hover',JSON.stringify({...uncertain,viewport:observed.viewport}));
    const approximate=await until(async()=>{const n=JSON.parse((await driver('selection-state')).details||'{}');return n.diagnosticId===uncertain.diagnosticId&&n.highlighted&&n.highlightVerification==='approximate_position_only'&&n});
    assert(Math.abs(approximate.highlightBounds.y-(uncertain.bounds.y-observed.viewport.pageY))<2);assert(!approximate.targetAvailable);
    await driver('select-element','Other scrolled control');
    await driver('page-hover',JSON.stringify({...button,viewport:observed.viewport}));
    const selected=await until(async()=>{const n=JSON.parse((await driver('selection-state')).details||'{}');return n.name==='Scrolled control'&&n.highlighted&&!n.hoverError&&!n.highlightError&&n});
    assert(Math.abs(selected.highlightBounds.y-(button.bounds.y-observed.viewport.pageY))<2);
    observed=await tool('observe',{...scope(),maxNodes:1000});
    await tool('interact',{...mutate(),action:'scroll',observationId:observed.observationId,direction:'down',amount:100});
    observed=await until(async()=>{const o=await tool('observe',{...scope(),maxNodes:1000});return o.viewport.pageY>=1700&&o});
    await until(async()=>{const n=JSON.parse((await driver('selection-state')).details||'{}');return n.name==='Scrolled control'&&n.highlighted&&n.viewport.pageY===observed.viewport.pageY&&Math.abs(n.highlightBounds.y-(button.bounds.y-observed.viewport.pageY))<2});
    await driver('page-hover',JSON.stringify({...button,viewport:observed.viewport}));
    await until(async()=>{const n=JSON.parse((await driver('selection-state')).details||'{}');return n.name==='Scrolled control'&&n.highlighted&&!n.hoverError});
    observed=await tool('observe',{...scope(),maxNodes:1000});
    await tool('interact',{...mutate(),action:'scroll',observationId:observed.observationId,direction:'up',amount:10000});
    await until(async()=>{const s=await driver();return s.labels.includes('Visible action')});
  });
  await check('Tab switching replaces evidence with the selected document',async()=>{
    const second={...worker,...await tool('tab_create',{agentSessionId:worker.agentSessionId,workspaceId:worker.workspaceId,url:base+'/second'})};
    await until(async()=>{const r=await raw('observe',{agentSessionId:second.agentSessionId,workspaceId:second.workspaceId,tabId:second.tabId,maxNodes:1000});return !r.isError&&r.structuredContent.nodes.some(n=>n.name==='Second rendered heading')});
    await driver('select-tab','Second elements fixture');await until(async()=>{const s=await driver();return s.labels.includes('Second rendered heading')&&!s.labels.includes('Rendered fixture heading')});
    await driver('select-tab','Developer elements fixture');await until(async()=>{const s=await driver();return s.labels.includes('Rendered fixture heading')&&!s.labels.includes('Second rendered heading')});
  });
  await check('Authentication protection removes diagnostic elements',async()=>{
    const observed=await tool('observe',{...scope(),maxNodes:1000}),button=observed.nodes.find(n=>n.name==='Protect fixture login'&&n.role==='button');
    await tool('interact',{...mutate(),action:'click',observationId:observed.observationId,elementRef:button.ref});
    await until(async()=>{const r=await raw('observe',scope());return r.isError});
    const state=await until(async()=>{const s=await driver();return s.count===0&&s});assert(!state.details.includes('SYNTHETIC_PASSWORD_CANARY'));
  });
  await check('Disable hides the panel and discards its rows',async()=>{await driver('toggle');await until(async()=>{const s=await driver();return !s.visible&&s.count===0});});
}catch(e){results.push({name:'Harness',passed:false,error:e.message});console.log(`FAIL harness: ${e.message}`);}
finally{
  if(client)await client.close().catch(()=>{});
  if(browser?.pid&&browser.exitCode===null)await runFile('taskkill.exe',['/PID',String(browser.pid),'/T','/F'],{windowsHide:true}).catch(()=>{});
  server?.close();const finalHash=await hash();const passed=results.length===(selectionOnly?2:20)&&results.every(r=>r.passed)&&initialHash===finalHash;
  const report={run,capturedAt:new Date().toISOString(),applicationDllSha256:initialHash,applicationDllSha256AtEnd:finalHash,selectionOnly,language:process.env.XENON_TEST_LANGUAGE??'en-US',theme:process.env.XENON_TEST_THEME??'dark',coverage:'Production CEF/MCP, native window/keyboard messages and PrintWindow on synthetic data; not physical input',profile,results,passed};
  const path=resolve(root,'out',`${run}.json`);await writeFile(path,JSON.stringify(report,null,2));console.log(`Report: ${path}`);process.exitCode=passed?0:1;
}
