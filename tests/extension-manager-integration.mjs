// Native fixture-driver coverage, not physical UI acceptance. The optional
// XENON_TEST_STORE_INSTALL run installs one fixed signed package only in its
// disposable profile, confirming its native dialog through a fixed UI driver.
import assert from 'node:assert/strict';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes, generateKeyPairSync } from 'node:crypto';
import { mkdir, readFile, writeFile, rename } from 'node:fs/promises';
import { resolve } from 'node:path';
import { createServer } from 'node:http';

const root=resolve(import.meta.dirname,'..');
const run=`extension-manager-integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const buildRoot=resolve(root,process.env.XENON_TEST_BUILD_DIR??'build');
// Keep native package paths below Windows' legacy file-operation length limit.
const profile=resolve(root,'.cache',`extension-manager-integration-${randomBytes(4).toString('hex')}`),binary=resolve(buildRoot,'auth-fixture/Release/XenonAuthTest.exe');
const dll=resolve(buildRoot,'auth-fixture/Release/XenonAuthTest.dll');
const hash=async()=>createHash('sha256').update(await readFile(dll)).digest('hex');
const results=[],runFile=promisify(execFile);let browser,debuggerProcess,id=0,initialHash,initialEngineHash;
let siteServer,siteOrigin;const quickIds={};
const surfaceCommands=new Map(),surfaceReports=[];
async function surfaceCommand(kind,command){
  const token=randomBytes(6).toString('hex');surfaceCommands.set(kind,{...command,token});
  return until(()=>surfaceReports.find(report=>report.token===token),15000);
}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function until(work,timeout=30000){const deadline=Date.now()+timeout;while(Date.now()<deadline){const value=await work();if(value)return value;await sleep(100);}throw new Error('Synthetic manager condition timed out');}
async function request(action){
  const current=++id;await writeFile(resolve(profile,'extension-manager-request.tmp'),JSON.stringify({id:current,action}));
  await until(async()=>{try{await rename(resolve(profile,'extension-manager-request.tmp'),resolve(profile,'extension-manager-request.json'));return true;}catch(error){if(error.code==='EPERM'||error.code==='EBUSY')return false;throw error;}},5000);
  return until(async()=>{
    if(browser.exitCode!==null)throw new Error(`Fixture exited: ${browser.exitCode}`);
    try{const value=JSON.parse(await readFile(resolve(profile,'extension-manager-result.json'),'utf8'));return value.id===current?value:false;}catch(e){if(e.code==='ENOENT'||e instanceof SyntaxError)return false;throw e;}
  },15000);
}
async function ready(action){return until(async()=>{const result=await request(action);if(!result.ok&&(result.error?.code==='NOT_READY'||action==='quick-list'&&result.error?.code==='stale_target'))return false;assert(result.ok,JSON.stringify(result));return result.result;},45000);}
async function check(name,work){const started=Date.now();try{const detail=await work();results.push({name,passed:true,elapsedMs:Date.now()-started,detail});console.log(`PASS ${name}`);}catch(error){results.push({name,passed:false,error:error.message});throw error;}}
try{
  initialHash=await hash();initialEngineHash=createHash('sha256').update(await readFile(resolve(buildRoot,'auth-fixture/Release/libcef.dll'))).digest('hex');await mkdir(profile,{recursive:true});
  await writeFile(resolve(profile,'SYNTHETIC_TEST_PROFILE'),'XENON_SYNTHETIC_EXTENSION_MANAGER_FIXTURE\n');
  if(process.env.XENON_TEST_STORE_INSTALL==='1')await writeFile(resolve(profile,'STORE_INSTALL_TEST_ALLOWED'),'XENON_SIGNED_STORE_INSTALL_FIXTURE:ddkjiahejlhfcafbddmgiahcphecmpfh\n');
  await writeFile(resolve(profile,'ui-settings.json'),JSON.stringify({version:1,introductionCompleted:true,automaticUpdateChecks:false,language:'en'}));
  await mkdir(resolve(profile,'temp'),{recursive:true});
  if(process.env.XENON_TEST_EXTENSION_QUICKLIST==='1'){
    siteServer=createServer((req,res)=>{
      res.setHeader('access-control-allow-origin','*');
      if(req.url?.startsWith('/command/')){const kind=req.url.slice(9),command=surfaceCommands.get(kind);surfaceCommands.delete(kind);res.writeHead(200,{'content-type':'application/json'});res.end(JSON.stringify(command??{}));return;}
      if(req.url==='/report'&&req.method==='POST'){let body='';req.on('data',bytes=>{body+=bytes;if(body.length>4096)req.destroy();});req.on('end',()=>{try{surfaceReports.push(JSON.parse(body));}catch{}res.end('ok');});return;}
      res.writeHead(200,{'content-type':'text/html'});res.end('<!doctype html><title>Extension site fixture</title><p>Synthetic extension site</p>');});
    await new Promise(resolve=>siteServer.listen(0,'127.0.0.1',resolve));
    const port=siteServer.address().port;siteOrigin=`http://127.0.0.1:${port}`;
    await writeFile(resolve(profile,'extension-quicklist-port.txt'),String(port));
    const records=[];
    for(const [label,enabled,pattern] of [['matching',true,'http://127.0.0.1/*'],['other',true,'https://unrelated.invalid/*'],['disabled',false,'http://127.0.0.1/*']]){
      const folder=randomBytes(8).toString('hex'),directory=resolve(profile,'extensions',folder);
      const {publicKey}=generateKeyPairSync('rsa',{modulusLength:2048}),key=publicKey.export({type:'spki',format:'der'});
      const extensionId=[...createHash('sha256').update(key).digest('hex').slice(0,32)].map(c=>String.fromCharCode(97+parseInt(c,16))).join('');
      quickIds[label]=extensionId;await mkdir(directory,{recursive:true});
      await writeFile(resolve(directory,'manifest.json'),JSON.stringify({manifest_version:3,name:`Synthetic ${label} extension`,version:'1.0',key:key.toString('base64'),host_permissions:[pattern],content_scripts:[{matches:[pattern],js:['content.js']}]}));
      await writeFile(resolve(directory,'content.js'),'document.documentElement.dataset.xenonExtensionFixture="active";\n');
      records.push({id:extensionId,name:`Synthetic ${label} extension`,version:'1.0',folder,enabled});
    }
    if(process.env.XENON_TEST_EXTENSION_ACTIONS==='1'){
      for(const [kind,name] of [['popup','Synthetic action popup'],['click','Synthetic action click']]){
        const folder=randomBytes(8).toString('hex'),directory=resolve(profile,'extensions',folder);
        const {publicKey}=generateKeyPairSync('rsa',{modulusLength:2048}),key=publicKey.export({type:'spki',format:'der'});
        const extensionId=[...createHash('sha256').update(key).digest('hex').slice(0,32)].map(c=>String.fromCharCode(97+parseInt(c,16))).join('');quickIds[kind]=extensionId;
        await mkdir(directory,{recursive:true});
        const manifest={manifest_version:3,name,version:'1.0',key:key.toString('base64'),permissions:['activeTab','scripting','storage'],background:{service_worker:'worker.js'},action:kind==='popup'?{default_popup:'default.html'}:{}};
        await writeFile(resolve(directory,'manifest.json'),JSON.stringify(manifest));
        if(kind==='popup'){
          await writeFile(resolve(directory,'worker.js'),"chrome.runtime.onInstalled.addListener(()=>chrome.action.setPopup({popup:'actual.html'}));\n");
          for(const variant of ['default','actual']){
            await writeFile(resolve(directory,`${variant}.html`),`<!doctype html><meta charset="utf-8"><title>Extension action fixture</title><style>body{font:14px system-ui;width:280px;margin:16px}button{padding:8px}</style><p>Native extension popup (${variant})</p><button id="close">Close popup</button><script src="${variant}.js"></script>`);
            await writeFile(resolve(directory,`${variant}.js`),`document.querySelector('#close').addEventListener('click',()=>window.close());(async()=>{const views=chrome.extension.getViews({type:'popup'});const tabs=await chrome.tabs.query({active:true,currentWindow:true});if(tabs.length!==1||!tabs[0].active||!tabs[0].url?.startsWith('http://127.0.0.1:'))return;await chrome.scripting.executeScript({target:{tabId:tabs[0].id},func:(view,variant)=>{document.documentElement.dataset.xenonActionPopup='active';document.documentElement.dataset.xenonActionView=String(view);document.documentElement.dataset.xenonActionVariant=variant;},args:[views.includes(window),'${variant}']});})();`);
          }
        }else await writeFile(resolve(directory,'worker.js'),"chrome.action.onClicked.addListener(async(tab)=>{await chrome.scripting.executeScript({target:{tabId:tab.id},func:()=>{document.documentElement.dataset.xenonActionClicked='active';}});});\n");
        records.push({id:extensionId,name,version:'1.0',folder,enabled:true});
      }
    }
    if(process.env.XENON_TEST_EXTENSION_SURFACES==='1')for(const kind of ['api','side']){
      const name=kind==='api'?'Synthetic API popup':'Synthetic side panel';
      const folder=randomBytes(8).toString('hex'),directory=resolve(profile,'extensions',folder);
      const {publicKey}=generateKeyPairSync('rsa',{modulusLength:2048}),key=publicKey.export({type:'spki',format:'der'});
      const extensionId=[...createHash('sha256').update(key).digest('hex').slice(0,32)].map(c=>String.fromCharCode(97+parseInt(c,16))).join('');quickIds[kind]=extensionId;
      await mkdir(directory,{recursive:true});
      const manifest={manifest_version:3,name,version:'1.0',key:key.toString('base64'),permissions:['scripting','storage','alarms',...(kind==='api'?['activeTab']:['tabs','sidePanel'])],background:{service_worker:'worker.js'},action:kind==='api'?{default_popup:'view.html'}:{}};
      if(kind==='side'){manifest.host_permissions=['http://127.0.0.1/*'];manifest.side_panel={default_path:'view.html'};}
      await writeFile(resolve(directory,'manifest.json'),JSON.stringify(manifest));
      const prelude=`const origin=${JSON.stringify(siteOrigin)};const report=value=>fetch(origin+'/report',{method:'POST',body:JSON.stringify(value)});`;
      await writeFile(resolve(directory,'worker.js'),prelude+`let busy=false,nativeWorkspace=false;async function poll(){if(busy)return;busy=true;try{if(!nativeWorkspace){const nativeTabs=await chrome.tabs.query({active:true,currentWindow:true});if(!nativeTabs.some(tab=>tab.windowId>=0x10000000))return;nativeWorkspace=true}const c=await (await fetch(origin+'/command/${kind}')).json();if(!c.token)return;let value={token:c.token,kind:'${kind}'};try{${kind==='api'?`await chrome.action.openPopup(c.options??{});value.opened=true;`:`const tabs=await chrome.tabs.query({active:true,currentWindow:true});const tab=tabs[0];if(c.action==='close')await chrome.sidePanel.close({windowId:tab.windowId});else if(c.action==='disable')await chrome.sidePanel.setOptions({enabled:false});else if(c.action==='enable')await chrome.sidePanel.setOptions({enabled:true,path:'view.html'});else if(c.action==='contextual')await chrome.sidePanel.setOptions({tabId:tab.id,path:'context.html',enabled:true});else if(c.action==='api-mode')await chrome.sidePanel.setPanelBehavior({openPanelOnActionClick:false});else await chrome.sidePanel.open({tabId:tab.id});value.done=true;value.tabId=tab.id;value.windowId=tab.windowId;`}}catch(e){value.error=String(e.message)}await report(value)}catch(e){}finally{busy=false}}chrome.runtime.onInstalled.addListener(async()=>{${kind==='side'?`await chrome.sidePanel.setPanelBehavior({openPanelOnActionClick:true});`:''}chrome.alarms.create('poll',{periodInMinutes:0.5});setInterval(poll,250)});chrome.alarms.onAlarm.addListener(poll);${kind==='side'?`chrome.action.onClicked.addListener(async tab=>{try{await chrome.sidePanel.open({tabId:tab.id});await report({kind:'side-open-api',opened:true})}catch(e){await report({kind:'side-open-api',error:String(e.message)})}});chrome.tabs.onActivated.addListener(value=>report({kind:'side-tab-activated',...value}));chrome.sidePanel.onOpened.addListener(value=>report({kind:'side-opened-event',...value}));chrome.sidePanel.onClosed.addListener(value=>report({kind:'side-closed-event',...value}));`:''}`);
      for(const variant of kind==='side'?['view','context']:['view']){
        await writeFile(resolve(directory,`${variant}.html`),`<!doctype html><meta charset="utf-8"><title>Synthetic extension surface</title><style>body{font:14px system-ui;margin:16px;min-width:220px}</style><h3>${name}</h3><p>${variant}</p><script src="${variant}.js"></script>`);
        await writeFile(resolve(directory,`${variant}.js`),prelude+`(async()=>{const tabs=await chrome.tabs.query({active:true,currentWindow:true});let denied=false;try{await chrome.scripting.executeScript({target:{tabId:tabs[0].id},func:()=>{document.documentElement.dataset.xenonSidePanel='active'}})}catch(e){denied=true}const contexts=await chrome.runtime.getContexts({contextTypes:['${kind==='api'?'POPUP':'SIDE_PANEL'}']});await report({kind:'${kind}-view',realView:contexts.some(c=>c.documentUrl===location.href),urlScrubbed:!tabs[0]?.url,injectionDenied:denied,windowId:tabs[0]?.windowId,variant:'${variant}'});})();`);
      }
      records.push({id:extensionId,name,version:'1.0',folder,enabled:true});
    }
    await writeFile(resolve(profile,'extensions','extensions.json'),JSON.stringify({version:1,extensions:records}));
  }
  if(process.env.XENON_TEST_LEGACY_EXTENSION==='1'&&process.env.XENON_TEST_EXTENSION_QUICKLIST!=='1'){
    const folder='0123456789abcdef',directory=resolve(profile,'extensions',folder);
    const {publicKey}=generateKeyPairSync('rsa',{modulusLength:2048});const key=publicKey.export({type:'spki',format:'der'});
    const extensionId=[...createHash('sha256').update(key).digest('hex').slice(0,32)].map(c=>String.fromCharCode(97+parseInt(c,16))).join('');
    await mkdir(directory,{recursive:true});await writeFile(resolve(directory,'manifest.json'),JSON.stringify({manifest_version:3,name:'Synthetic fixture extension',version:'1.0',key:key.toString('base64'),background:{service_worker:'worker.js'}}));
    await writeFile(resolve(directory,'worker.js'),'chrome.runtime.onInstalled.addListener(()=>{});\n');
    await writeFile(resolve(profile,'extensions','extensions.json'),JSON.stringify({version:1,extensions:[{id:extensionId,name:'Synthetic fixture extension',version:'1.0',folder,enabled:true}]}));
  }
  browser=spawn(binary,[`--user-data-dir=${profile}`,`--broker-pipe=xenon-${run}`,'--test-extension-manager'],{windowsHide:true,stdio:'ignore',env:{...process.env,TEMP:resolve(profile,'temp'),TMP:resolve(profile,'temp')}});
  let launchError;browser.once('error',error=>{launchError=error;});
  await until(async()=>{if(launchError)throw launchError;if(browser.exitCode!==null)throw new Error(`Fixture exited during startup: ${browser.exitCode}`);try{await readFile(resolve(profile,'broker-state.json'));return true;}catch{return false;}});
  if(process.env.XENON_TEST_DEBUGGER){
    await mkdir(resolve(root,'out'),{recursive:true});
    debuggerProcess=spawn(process.env.XENON_TEST_DEBUGGER,['-p',String(browser.pid),'-y',[resolve(buildRoot,'auth-fixture/Release'),process.env.XENON_TEST_SYMBOL_PATH].filter(Boolean).join(';'),'-logo',resolve(root,'out',`${run}-debugger.log`),'-c','sxe -c ".echo XENON_FIXTURE_BREAKPOINT; .ecxr; kv; q" bpe; sxe -c ".echo XENON_FIXTURE_ACCESS_VIOLATION; .ecxr; kv; q" av; g'],{windowsHide:true,stdio:'ignore'});
    await sleep(2000);
  }
  let originalTabs;
  if(process.env.XENON_TEST_EXTENSION_SURFACES==='1'){
    console.log('Waiting for the Xenon test window to be active...');
    await until(async()=>(await ready('panel-foreground')).foreground,120000);await sleep(500);
  }
  if(process.env.XENON_TEST_EXTENSION_QUICKLIST==='1'){
    await check('Quick list contains enabled extensions without opening a visible manager or broker tab',async()=>{
      const before=await until(async()=>{const value=await ready('inventory');return value.brokerTabs>0?value:false;});
      const list=await until(async()=>{const value=await ready('quick-list');return value.extensions.some(e=>e.id===quickIds.matching)&&value.extensions.some(e=>e.id===quickIds.other)?value:false;});
      assert(!list.extensions.some(e=>e.id===quickIds.disabled));
      const after=await ready('inventory');assert.equal(after.managerOpen,false);assert.equal(after.brokerTabs,before.brokerTabs);
      return {enabledFixtures:2,disabledExcluded:true,visibleManagerOpened:false,brokerTabsUnchanged:true};
    });
    await check('Quick list distinguishes site access from other extensions on a real content-script page',async()=>{
      await ready('quick-site');await until(async()=>(await ready('quick-marker')).contentScriptActive);
      if(process.env.XENON_TEST_EXTENSION_SURFACES==='1')await until(async()=>{const s=await ready('panel-foreground');return s.foreground&&!s.loading&&s.loopbackReady;});
      const list=await ready('quick-list');assert.equal(list.origin,siteOrigin);
      assert.equal(list.extensions.find(e=>e.id===quickIds.matching)?.access,'allowed');
      assert.equal(list.extensions.find(e=>e.id===quickIds.other)?.access,'other');
      assert(!list.extensions.some(e=>e.id===quickIds.disabled));return {matchingAccess:true,otherAccess:false,contentScriptRan:true,disabledExcluded:true};
    });
    await check('Quick list withholds a result after its source page navigates',async()=>{
      const result=await request('quick-stale');assert.equal(result.ok,false);assert.equal(result.error?.code,'stale_target');return {staleResultWithheld:true};
    });
  }
  if(process.env.XENON_TEST_EXTENSION_SURFACES==='1'){
    await ready('panel-outside');await ready('quick-site');await until(async()=>(await ready('quick-marker')).fixturePageReady);
    await until(async()=>{const s=await ready('panel-foreground');return s.foreground&&!s.loading&&s.loopbackReady;});
    await check('Programmatic openPopup resolves after a genuine popup loads without granting activeTab',async()=>{
      const value=await surfaceCommand('api',{});assert.equal(value.opened,true,JSON.stringify({value,state:await ready('popup-state')}));
      const view=await until(()=>surfaceReports.find(r=>r.kind==='api-view'));assert(view.realView);assert(view.urlScrubbed);assert(view.injectionDenied);
      assert.equal((await ready('popup-state')).popups,1);return {realPopup:true,promiseResolved:true,urlPermissionScrubbed:true,scriptPermissionNotGranted:true};
    });
    await check('Programmatic popup rejection preserves an existing popup and rejects unknown windows',async()=>{
      const conflict=await surfaceCommand('api',{});assert(conflict.error);assert.equal((await ready('popup-state')).popups,1);
      await ready('panel-outside');await until(async()=>(await ready('popup-state')).popups===0);
      const unknown=await surfaceCommand('api',{options:{windowId:2147483000}});assert(unknown.error);assert.equal((await ready('popup-state')).popups,0);
      const windowId=surfaceReports.find(r=>r.kind==='api-view').windowId;const explicit=await surfaceCommand('api',{options:{windowId}});assert(explicit.opened,JSON.stringify(explicit));await ready('panel-outside');await until(async()=>(await ready('popup-state')).popups===0);
      return {existingPopupPreserved:true,invalidWindowRejected:true,explicitWindowTarget:true};
    });
    await check('Toolbar action opens a genuine docked side panel and reserves website space',async()=>{
      await ready('action-side');await until(async()=>(await ready('popup-state')).sidePanels===1);
      const view=await until(()=>surfaceReports.find(r=>r.kind==='side-view'));assert(view.realView);assert.equal(view.injectionDenied,false);
      const state=await until(async()=>{const s=await ready('panel-dock-state');return s.dockVisible&&s.dockWidth>=200&&s.pageWidth>100?s:false;});
      await ready('panel-dock-resize');const resized=await ready('panel-dock-state');assert(resized.dockWidth>state.dockWidth);assert(resized.pageWidth<state.pageWidth);
      await writeFile(resolve(root,'out',`${run}-side-panel.png`),await readFile(resolve(profile,'extension-panel-render.png')));
      await until(()=>surfaceReports.some(r=>r.kind==='side-opened-event'));return {realSidePanelView:true,dockWidth:state.dockWidth,pageWidth:state.pageWidth,openedEvent:true};
    });
    await check('Global side panel remains docked across same-workspace tab changes and closes through its API',async()=>{
      const activations=surfaceReports.filter(r=>r.kind==='side-tab-activated').length;
      await ready('panel-dock-new-tab');await until(async()=>(await ready('panel-dock-state')).dockVisible);
      console.log('Verified global dock on newly created tab');
      await until(()=>surfaceReports.filter(r=>r.kind==='side-tab-activated').length>activations);
      await ready('panel-dock-return');await until(async()=>(await ready('panel-dock-state')).dockVisible);
      const close=await surfaceCommand('side',{action:'close'});assert(close.done,JSON.stringify(close));
      await until(async()=>(await ready('popup-state')).sidePanels===0);await until(()=>surfaceReports.some(r=>r.kind==='side-closed-event'));
      return {globalAcrossTabs:true,apiClosed:true,closedEvent:true};
    });
    await check('Disabling side-panel options retires the existing host',async()=>{
      await ready('side-button');await until(async()=>(await ready('popup-state')).sidePanels===1);
      const disabled=await surfaceCommand('side',{action:'disable'});assert(disabled.done,JSON.stringify(disabled));await until(async()=>(await ready('popup-state')).sidePanels===0);
      const enabled=await surfaceCommand('side',{action:'enable'});assert(enabled.done);return {disabledHostClosed:true,optionsRestored:true};
    });
    await check('Tab-specific side panel hides on other tabs and restores on its source tab',async()=>{
      const set=await surfaceCommand('side',{action:'contextual'});assert(set.done,JSON.stringify(set));await ready('side-button');
      await until(()=>surfaceReports.some(r=>r.kind==='side-view'&&r.variant==='context'));
      await until(async()=>(await ready('panel-dock-state')).dockVisible);
      await ready('panel-dock-new-tab');await until(async()=>!(await ready('panel-dock-state')).dockVisible);
      await ready('panel-dock-return');await until(async()=>(await ready('panel-dock-state')).dockVisible);await ready('side-close');
      return {contextualPath:true,hiddenOnOtherTab:true,restoredOnReturn:true};
    });
    await check('sidePanel.open preserves Chromium user-gesture enforcement and works from onClicked',async()=>{
      const noGesture=await surfaceCommand('side',{action:'open'});assert(noGesture.error?.includes('user gesture'),JSON.stringify(noGesture));
      const mode=await surfaceCommand('side',{action:'api-mode'});assert(mode.done);await ready('action-side');
      await until(()=>surfaceReports.some(r=>r.kind==='side-open-api'&&r.opened));await until(async()=>(await ready('popup-state')).sidePanels===1);
      return {nonGestureRejected:true,onClickedGestureOpened:true};
    });
    await check('Protected source interaction closes panels and rejects extension-requested popups',async()=>{
      await ready('side-protect');await until(async()=>(await ready('popup-state')).sidePanels===0);
      const blocked=await surfaceCommand('api',{});assert(blocked.error);assert.equal((await ready('popup-state')).popups,0);await ready('side-unprotect');
      return {panelClosed:true,popupBlocked:true};
    });
    await check('Private selection retires side panels and refuses extension-requested popups',async()=>{
      await ready('side-button');await until(async()=>(await ready('popup-state')).sidePanels===1);
      await ready('panel-dock-private');await until(async()=>(await ready('popup-state')).sidePanels===0);
      const blocked=await surfaceCommand('api',{});assert(blocked.error);assert.equal((await ready('popup-state')).popups,0);await ready('panel-dock-return');
      return {privatePanelClosed:true,privatePopupRejected:true};
    });
    await ready('side-close');await ready('popup-close');
  }
  if(process.env.XENON_TEST_EXTENSION_ACTIONS==='1'){
    await check('Compact extension panel renders icons, site switch and per-extension controls',async()=>{
      await ready('quick-site');await until(async()=>(await ready('quick-marker')).contentScriptActive);
      if(process.env.XENON_TEST_EXTENSION_SURFACES==='1')await until(async()=>{const s=await ready('panel-foreground');return s.foreground&&!s.loading&&s.loopbackReady;});
      await ready('panel-open');const state=await until(async()=>{const value=await ready('panel-state');return value.visible?value:false;});
      assert(state.rows>=4);assert.equal(state.siteToggle,true);assert.equal(state.iconRows,state.rows);assert(state.widthDip<=365&&state.heightDip<600);
      await writeFile(resolve(root,'out',`${run}-extensions-panel.png`),await readFile(resolve(profile,'extension-panel-render.png')));return state;
    });
    await check('Native panel pin button updates the workspace toolbar and persists its state',async()=>{
      await ready('panel-pin');await until(async()=>{const list=await ready('quick-list');return list.extensions.find(e=>e.id===quickIds.popup)?.pinned;});
      const state=await until(async()=>{const value=await ready('panel-state');return value.visible&&value.pinnedButtons>=1?value:false;});return {pinned:true,nativeToolbarButton:true,panelRemainsOpen:true};
    });
    await check('Native row opens a genuine dynamic action popup with current-tab activeTab permission',async()=>{
      await ready('panel-activate');
      const state=await until(async()=>{const value=await ready('popup-marker');return value.popupActiveTab&&value.popupView&&value.popupDynamic?value:false;});
      assert.equal((await ready('quick-list')).extensions.find(e=>e.id===quickIds.popup)?.access,'allowed','Temporary activeTab permission is missing from the panel status');
      assert.equal((await ready('popup-state')).popups,1);return {...state,hostPermissionNotDeclared:true,nativePopupView:true};
    });
    await check('Clicking outside dismisses the action popup',async()=>{
      await ready('panel-outside');await until(async()=>(await ready('popup-state')).popups===0);return {dismissed:true};
    });
    await check('Pinned extension button opens its action popup',async()=>{
      await ready('panel-pinned-action');await until(async()=>(await ready('popup-state')).popups===1);return {pinnedButtonDispatched:true};
    });
    await check('Navigation closes the popup and invalidates its source page',async()=>{
      const stale=await request('quick-stale');assert.equal(stale.ok,false);assert.equal(stale.error?.code,'stale_target');
      await until(async()=>(await ready('popup-state')).popups===0);return {closedOnNavigation:true};
    });
    await check('Actions without popup dispatch the real Chromium onClicked event',async()=>{
      await ready('quick-site');await until(async()=>(await ready('quick-marker')).contentScriptActive);await ready('action-click');
      await until(async()=>(await ready('popup-marker')).actionClicked);return {nativeOnClicked:true,pageChangedWithActiveTab:true};
    });
    await check('Native site switch blocks action invocation without opening a popup',async()=>{
      await ready('panel-open');await until(async()=>(await ready('panel-state')).visible);await ready('panel-site-toggle');
      await until(async()=>(await ready('quick-list')).siteBlocked);const result=await request('action-popup');assert.equal(result.ok,false);assert.equal(result.error?.code,'extension_site_blocked');
      assert.equal((await ready('popup-state')).popups,0);
      await ready('quick-site');await until(async()=>(await ready('quick-marker')).fixturePageReady);await sleep(500);assert.equal((await ready('quick-marker')).contentScriptActive,false,'Blocked extension still injected into the new document');
      await ready('action-allow-site');await until(async()=>!(await ready('quick-list')).siteBlocked);await ready('quick-site');await until(async()=>(await ready('quick-marker')).contentScriptActive);
      return {siteBlocked:true,invocationRefused:true,contentScriptBlocked:true,restored:true};
    });
    await check('Per-extension site access withholds and restores a real content script',async()=>{
      await ready('action-on-click-matching');await ready('quick-site');await until(async()=>(await ready('quick-marker')).fixturePageReady);await sleep(500);assert.equal((await ready('quick-marker')).contentScriptActive,false);
      assert.equal((await ready('quick-list')).extensions.find(e=>e.id===quickIds.matching)?.access,'on_click');
      await ready('action-on-site-matching');await ready('quick-site');await until(async()=>(await ready('quick-marker')).contentScriptActive);return {withheld:true,restored:true};
    });
    await check('Site permission changes cannot grant an origin outside the manifest request',async()=>{
      const result=await request('action-on-site-other');assert.equal(result.ok,false);assert.equal((await ready('quick-list')).extensions.find(e=>e.id===quickIds.other)?.access,'other');return {unrequestedOriginRejected:true};
    });
    await ready('popup-close');await ready('panel-outside');
  }
  await check('Native Chrome extension manager uses the persistent workspace profile',async()=>{
    originalTabs=await until(async()=>{const value=await ready('inventory');return value.brokerTabs>0?value.brokerTabs:false;});await ready('manager');
    const state=await until(async()=>{const value=await ready('probe');return value.managerPage&&value.managerElement&&value.developerApi?value:false;});
    assert.equal((await ready('inventory')).brokerTabs,originalTabs,'Manager entered the broker tab inventory');return state;
  });
  await check('Repeated open reuses the workspace manager without new broker tabs',async()=>{
    await ready('manager');assert.equal((await ready('inventory')).brokerTabs,originalTabs);return {reused:true};
  });
  await check('Extension page is embedded in Xenon without a Chromium browser frame',async()=>{
    const state=await until(async()=>{const value=await ready('inventory');return value.fitsHost&&value.paintedContent?value:false;});
    assert.equal(state.insideXenon,true);assert.equal(state.childWindow,true);assert.equal(state.noBrowserFrame,true);assert.equal(state.viewsVisible,true,'Views compositor is hidden despite the native HWND being visible');
    await mkdir(resolve(root,'out'),{recursive:true});await writeFile(resolve(root,'out',`${run}-manager.png`),await readFile(resolve(profile,'extension-manager-render.png')));return state;
  });
  await check('Drawer hides Chrome logos and its actual Store link opens the Store in Xenon',async()=>{
    assert.equal((await ready('drawer')).clicked,true);
    const state=await until(async()=>{const value=await ready('probe');return value.drawerOpen&&value.drawerLogoHidden&&value.storeLogoHidden?value:false;});
    await ready('inventory');await writeFile(resolve(root,'out',`${run}-drawer.png`),await readFile(resolve(profile,'extension-manager-render.png')));
    assert.equal((await ready('native-sidebar-link')).clicked,true);
    await until(async()=>{const value=await ready('probe');return value.storePage&&value.storeApi;},60000);
    assert.equal((await ready('inventory')).brokerTabs,originalTabs);return state;
  });
  if(process.env.XENON_TEST_EXTENSION_QUICKLIST!=='1')await check('Empty-list Store link also opens the real Store with native installation bindings',async()=>{
    await ready('manager');await until(async()=>{const value=await ready('probe');return value.managerPage&&value.managerElement;});
    await until(async()=>(await ready('empty-store-link')).clicked);
    await until(async()=>{const value=await ready('probe');return value.storePage&&value.storeApi;},60000);
    assert.equal((await ready('inventory')).brokerTabs,originalTabs);return {linkOpenedStore:true};
  });
  await check('Chrome Web Store receives Chromium native installation API bindings',async()=>{
    await ready('store');const state=await until(async()=>{const value=await ready('probe');return value.storePage&&value.storeApi?value:false;},60000);
    assert.equal((await ready('inventory')).brokerTabs,originalTabs);return state;
  });
  await check('Native Store install permission prompt can be canceled by closing its manager',async()=>{
    assert.equal((await ready('install-prompt')).clicked,true);await sleep(3000);
    assert.equal(browser.exitCode,null,'Browser crashed while preparing the install permission prompt');
    await ready('close');await until(async()=>!(await ready('inventory')).managerOpen);
    await ready('manager');await until(async()=>(await ready('probe')).managerPage);return {syntheticManifest:true,installationNotApproved:true,browserSurvived:true};
  });
  await check('Chromium-created untracked window is rejected without crashing its caller',async()=>{
    await ready('untracked-window');await sleep(1500);assert.equal(browser.exitCode,null,'Browser crashed during untracked window creation');
    assert.equal((await ready('probe')).managerPage,true);assert.equal((await ready('inventory')).auxiliaryBrowsers,0);return {browserSurvived:true,auxiliaryDrained:true};
  });
  if(process.env.XENON_TEST_STORE_INSTALL==='1')await check('Mismatched Store manifest is rejected without crashing the embedded browser',async()=>{
    await ready('install-page');await until(async()=>(await ready('probe')).storePage);
    assert.equal((await ready('install-mismatch')).clicked,true);
    await until(async()=>(await ready('confirm-install')).clicked,30000);
    await until(async()=>(await ready('install-rejected')).installRejected,120000);
    assert.equal((await ready('installed')).installed,false);return {nativePermissionConfirmed:true,manifestMismatchRejected:true,extensionNotInstalled:true,browserSurvived:true};
  });
  if(process.env.XENON_TEST_STORE_INSTALL==='1')await check('Store signed-package installation completes in its disposable workspace',async()=>{
    await ready('install-page');await until(async()=>(await ready('install-click')).clicked,60000);
    await until(async()=>(await ready('confirm-install')).clicked,30000);
    await until(async()=>(await ready('installed')).installed,120000);
    await sleep(3000);assert.equal(browser.exitCode,null);assert.equal((await ready('inventory')).auxiliaryBrowsers,0);return {nativePermissionConfirmed:true,storeReportsInstalled:true,disposableProfile:true,auxiliaryDrained:true};
  });
  if(process.env.XENON_TEST_EXTENSION_QUICKLIST==='1'&&process.env.XENON_TEST_STORE_INSTALL==='1')await check('Quick list immediately includes a newly installed signed Store extension',async()=>{
    const list=await ready('quick-list');assert(list.extensions.some(e=>e.id==='ddkjiahejlhfcafbddmgiahcphecmpfh'));
    return {storeExtensionListed:true,restartRequired:false};
  });
  await check('Google account route requires a Store visit and can return to Store',async()=>{
    await ready('manager');await until(async()=>(await ready('probe')).managerPage);
    await ready('account-direct');await until(async()=>(await ready('inventory')).accountBlocked);
    assert.equal((await ready('probe')).managerPage,true,'Account navigation opened without an active Store visit');
    await ready('store');await until(async()=>{const value=await ready('probe');return value.storePage&&value.storeApi;});
    await ready('account-redirect');
    const account=await until(async()=>{const value=await ready('probe');return value.accountPage?value:false;});
    assert.equal(account.developerApi,false);assert.equal(account.storeApi,false);
    await ready('store');const state=await until(async()=>{const value=await ready('probe');return value.storePage&&value.storeApi?value:false;});
    assert.equal((await ready('inventory')).accountBlocked,false);
    assert.equal((await ready('inventory')).brokerTabs,originalTabs);
    await ready('manager');await until(async()=>(await ready('probe')).managerPage);
    await ready('account-direct');await until(async()=>(await ready('inventory')).accountBlocked);
    assert.equal((await ready('probe')).managerPage,true,'Account authority survived returning to Extensions');
    return {fixedAccountNavigation:true,accountPageHasNoPrivilegedApis:true,returnedToStore:true,storeApi:state.storeApi,directAccountBlocked:true,returnToManagerRevokesAccount:true};
  });
  await check('Store navigation works when Extensions is the only remaining tab',async()=>{
    await ready('close-pages');await until(async()=>(await ready('inventory')).brokerTabs===0);originalTabs=0;
    await ready('manager');await until(async()=>{const value=await ready('probe');return value.managerPage&&value.developerApi;});
    await ready('store');await until(async()=>{const value=await ready('probe');return value.storePage&&value.storeApi;},60000);return {ordinaryTabs:0};
  });
  await check('Closing the manager preserves workspace tabs and permits reopening',async()=>{
    await ready('close');await until(async()=>!(await ready('inventory')).managerOpen);
    assert.equal((await ready('inventory')).brokerTabs,originalTabs);await ready('manager');
    await until(async()=>{const state=await ready('probe');return state.managerPage&&state.developerApi;});return {closed:true,reopened:true};
  });
  await check('Browser shutdown drains embedded manager before CEF exits',async()=>{
    await request('shutdown');await until(()=>browser.exitCode!==null,20000);assert.equal(browser.exitCode,0);return {cleanExit:true};
  });
}catch(error){if(!results.some(r=>!r.passed))results.push({name:'Manager harness',passed:false,error:error.message});console.log(`FAIL ${error.message}`);}
finally{
  if(browser&&browser.exitCode===null)await runFile('taskkill.exe',['/PID',String(browser.pid),'/T','/F'],{windowsHide:true}).catch(()=>{});
  if(siteServer){siteServer.closeAllConnections();await new Promise(resolve=>siteServer.close(resolve));}
  if(debuggerProcess&&debuggerProcess.exitCode===null)debuggerProcess.kill();
  const finalHash=await hash();assert.equal(finalHash,initialHash,'Browser was rebuilt during validation');
  assert.equal(createHash('sha256').update(await readFile(resolve(buildRoot,'auth-fixture/Release/libcef.dll'))).digest('hex'),initialEngineHash,'Engine was rebuilt during validation');
  await mkdir(resolve(root,'out'),{recursive:true});const report={run,profile,testDriver:true,physicalUi:false,applicationDllSha256:initialHash,engineDllSha256:initialEngineHash,passed:results.every(r=>r.passed),results};
  await writeFile(resolve(root,'out',`${run}.json`),JSON.stringify(report,null,2));console.log(`Report: out/${run}.json`);process.exitCode=report.passed?0:1;
}
