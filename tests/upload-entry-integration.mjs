// Real MCP + production CEF, synthetic native-approved files only.
// --benchmark <source> copies an external fixture unchanged into this run's
// .cache directory; all exported calls receive an explicit synthetic root.
// --baseline records the alpha.6 custom-button failure as a reproduction,
// never as acceptance of the fixed browser. Original evidence is never written.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { basename, resolve } from 'node:path';
import { createRequire } from 'node:module';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';

const root = resolve(import.meta.dirname, '..');
const buildRoot = resolve(root, process.env.XENON_TEST_BUILD_DIR ?? 'build');
const run = 'upload-entry-' + Date.now() + '-' + randomBytes(4).toString('hex');
const profile = resolve(root, '.cache', 'auth-integration-' + run), uploadsRoot = profile + '-uploads';
const pipeName = 'xenon-test-' + run, slash = String.fromCharCode(92), pipe = slash + slash + '.' + slash + 'pipe' + slash + pipeName;
const binary = resolve(buildRoot, 'app/Release/Xenon.exe'), dll = resolve(buildRoot, 'app/Release/Xenon.dll');
const baseline = process.argv.includes('--baseline'), benchmarkIndex = process.argv.indexOf('--benchmark');
const oracleObservation = process.argv.includes('--oracle-observation');
if (oracleObservation && (baseline || benchmarkIndex >= 0)) throw new Error('--oracle-observation is an isolated acceptance case');
if (benchmarkIndex >= 0 && !process.argv[benchmarkIndex + 1]) throw new Error('--benchmark requires the source path');
const benchmarkSource = benchmarkIndex >= 0 ? resolve(process.argv[benchmarkIndex + 1]) : undefined;
const benchmarkMatrix = process.argv.includes('--benchmark-matrix');
if (benchmarkMatrix && (!benchmarkSource || baseline)) throw new Error('--benchmark-matrix requires --benchmark and acceptance mode');
const workspaceId = 'auth_fixture_shared';
const results = [], calls = [], servers = [], fixtureStates = new Map();
const runFile = promisify(execFile), sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const hashDll = async () => sha256(await readFile(dll));
let browser, client, noUploadsClient, worker, otherWorker, fileId, folderId, origin, crossOrigin, applicationDllSha256, applicationDllSha256AtEnd;
let lastObservation, benchmarkMetadata, benchmarkModule, benchmarkRoot, benchmarkId, benchmarkTrial, benchmarkServer;

async function until(action, timeout = 15000, label = 'Fixture condition') {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) { const value = await action(); if (value) return value; await sleep(100); }
  throw new Error(label + ' timed out');
}
async function check(name, action) {
  const started = Date.now();
  try { const details = await action(); results.push({ name, passed: true, elapsedMs: Date.now() - started, details }); console.log('PASS ' + name); }
  catch (error) { results.push({ name, passed: false, elapsedMs: Date.now() - started, error: error.message }); throw error; }
}
async function raw(name, args = {}) {
  const started = Date.now();
  const response = await client.callTool({ name: 'xenon_' + name, arguments: args });
  calls.push({ name: 'xenon_' + name, args, elapsedMs: Date.now() - started, isError: !!response.isError, result: response.structuredContent });
  return response;
}
async function tool(name, args = {}) {
  const response = await raw(name, args);
  assert(!response.isError, name + ': ' + JSON.stringify(response.structuredContent ?? response.content)); return response.structuredContent;
}
const workerScope = value => ({ agentSessionId: value.agentSessionId, workspaceId: value.workspaceId });
const scope = tab => ({ ...workerScope(tab), tabId: tab.tabId });
const mutation = tab => ({ ...scope(tab), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID() });
function selectionFacts(response, activation, selection) {
  if (baseline) return;
  const facts = response.isError ? response.structuredContent?.error : response.structuredContent;
  assert.equal(facts?.activation, activation, 'Upload activation outcome was not explicit');
  assert.equal(facts?.fileSelection, selection, 'File selection outcome was not explicit');
}
async function observe(tab) {
  const deadline = Date.now() + 5000;
  for (;;) {
    const response = await raw('observe', { ...scope(tab), maxNodes: 1000 }); lastObservation = response;
    // A cross-origin frame attaching or finishing its parse changes the tab's evidence. Retry only
    // these explicitly transient reads; never replay a mutation.
    if (response.isError && ['privacy_guard_initializing', 'page_changed'].includes(response.structuredContent?.error?.code) && Date.now() < deadline) { await sleep(100); continue; }
    assert(!response.isError, 'observe: ' + JSON.stringify(response.structuredContent ?? response.content)); return response.structuredContent;
  }
}
async function scroll(tab, observation, direction, amount) {
  await tool('interact', { ...mutation(tab), observationId: observation.observationId, action: 'scroll', direction, amount });
  await sleep(120); return observe(tab);
}
async function target(tab, name, tag) {
  const matches = observation => observation.nodes.find(node => node.name === name && node.ref && (!tag || node.tag === tag));
  let observation = await observe(tab);
  if (matches(observation)) return { observation, node: matches(observation) };
  for (let index = 0; index < 3 && observation.viewport?.pageY > 0; index++) observation = await scroll(tab, observation, 'up', 3000);
  for (let index = 0; index < 12; index++) {
    const node = matches(observation); if (node) return { observation, node };
    const before = observation.viewport?.pageY;
    observation = await scroll(tab, observation, 'down', Math.max(100, Math.min(420, Math.floor((observation.viewport?.height ?? 600) / 2))));
    if (observation.viewport?.pageY === before) break;
  }
  throw new Error('Visible target missing: ' + name);
}
async function act(tab, name, action = 'click', fields = {}, tag = action === 'fill' ? 'INPUT' : action === 'select' ? 'SELECT' : action === 'check' ? 'INPUT' : 'BUTTON') {
  const { observation, node } = await target(tab, name, tag);
  return tool('interact', { ...mutation(tab), observationId: observation.observationId, action, elementRef: node.ref, ...fields });
}
async function upload(tab, name, grant = fileId, tag = 'BUTTON') {
  const { observation, node } = await target(tab, name, tag);
  return raw('upload', { ...mutation(tab), observationId: observation.observationId, elementRef: node.ref, fileId: grant });
}
async function open(caseId, mode, owner = worker) {
  const tab = { ...owner, ...await tool('tab_create', { ...workerScope(owner), url: origin + '/page?case=' + caseId + '&mode=' + mode }), caseId };
  // A cross-origin child page has its own ready heading; also require the parent's loading marker to be gone.
  await until(async () => { const { nodes } = await observe(tab);
    return nodes.some(node => node.name === 'Upload entry fixture ready') && !nodes.some(node => node.name === 'Loading upload entry fixture'); });
  await until(() => fixtureStates.get(caseId)?.opened); return tab;
}
const alivePipe = () => new Promise(resolve => {
  const socket = net.connect(pipe); socket.once('connect', () => { socket.destroy(); resolve(true); }); socket.once('error', () => { socket.destroy(); resolve(false); });
});
async function launch() {
  browser = spawn(binary, ['--user-data-dir=' + profile, '--broker-pipe=' + pipeName], { windowsHide: true, stdio: 'ignore' });
  let launchError; browser.once('error', error => { launchError = error; });
  await until(async () => { if (launchError) throw launchError; if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Owned browser exited'); return alivePipe(); }, 45000, 'Private browser pipe');
  client = new Client({ name: 'Upload entry regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(new StdioClientTransport({ command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', resolve(profile, 'client-config.json')], stderr: 'pipe' }));
  worker = await tool('worker_create', { name: 'Upload entry owner', workspaceId });
  otherWorker = await tool('worker_create', { name: 'Unrelated upload workspace' });
  const folders = await tool('folders', workerScope(worker)); assert(folders.folders.some(item => item.folderId === folderId));
  const files = await tool('files', { ...workerScope(worker), folderId, limit: 1000 });
  fileId = files.files.find(item => item.name === 'approved-report.txt')?.fileId; assert(fileId);
}
async function startFixtures() {
  const handler = (request, response) => {
    const url = new URL(request.url, 'http://127.0.0.1'), caseId = url.searchParams.get('case');
    if (!caseId || !/^[a-z-]+$/.test(caseId)) { response.writeHead(400).end(); return; }
    let state = fixtureStates.get(caseId);
    if (!state) { state = { opened: false, clicks: 0, trusted: [], changes: 0, uploads: 0, uploaded: false, changeLabel: false, labelChanged: false }; fixtureStates.set(caseId, state); }
    response.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/state') { response.setHeader('Content-Type', 'application/json'); response.end(JSON.stringify({ changeLabel: state.changeLabel })); return; }
    if (request.method === 'POST') {
      let chunks = []; let size = 0; request.on('data', chunk => { size += chunk.length; if (size > 16384) request.destroy(); else chunks.push(chunk); });
      request.on('end', () => {
        const body = Buffer.concat(chunks);
        if (url.pathname === '/upload') {
          ++state.uploads; state.uploaded = body.includes(Buffer.from('filename="approved-report.txt"')) && body.includes(Buffer.from('Xenon synthetic approved upload.\n'));
          response.setHeader('Content-Type', 'application/json'); response.end(JSON.stringify({ passed: state.uploaded }));
        } else if (url.pathname === '/event') {
          try { const value = JSON.parse(body); if (value.type === 'opened') state.opened = true; if (value.type === 'click') { ++state.clicks; state.trusted.push(value.trusted); (state.textHits ??= []).push(value.textHit === true); } if (value.type === 'other') { state.otherClicks = (state.otherClicks ?? 0) + 1; } if (value.type === 'change') { ++state.changes; state.selectedFiles = value.files; } if (value.type === 'label') state.labelChanged = true; response.end('ok'); }
          catch { response.writeHead(400).end(); }
        } else response.writeHead(404).end();
      }); return;
    }
    if (url.pathname !== '/page') { response.writeHead(404).end(); return; }
    const mode = url.searchParams.get('mode'), frame = mode === 'frame' || mode === 'frame-text';
    response.setHeader('Content-Type', 'text/html; charset=utf-8');
    if (mode === 'svg-dropzone') {
      response.end('<!doctype html><meta charset="utf-8"><title>SVG dropzone fixture ' + caseId + '</title>' +
        '<style>body{background:white;color:#152235;font:18px system-ui;margin:20px}#drop{position:fixed;left:40px;top:110px;width:220px;height:110px;border:2px dashed #152235}#file{position:absolute;inset:0;width:100%;height:100%;opacity:0;cursor:pointer}</style>' +
        '<h1>Upload entry fixture ready</h1>' +
        '<div id="drop"><svg width="220" height="110" viewBox="0 0 220 110" aria-hidden="true"><rect x="12" y="12" width="196" height="86" rx="8" fill="#eef4ff" stroke="#152235" stroke-width="2"/><path d="M110 30 L110 78 M86 54 L134 54" stroke="#152235" stroke-width="4"/></svg>' +
        '<input id="file" type="file" accept=".txt,text/plain,bad filter with spaces,C:\\secret\\path.txt"></div>' +
        '<p id="status" style="margin-top:160px">No file transferred</p><script>' +
        'const caseId=' + JSON.stringify(caseId) + ',file=document.querySelector("#file");' +
        'function event(v){return fetch("/event?case="+caseId,{method:"POST",body:JSON.stringify(v)})}event({type:"opened"});' +
        'file.onclick=e=>event({type:"click",trusted:e.isTrusted});' +
        'file.onchange=async()=>{event({type:"change",files:[...file.files].map(v=>v.name)});if(!file.files[0])return;const data=new FormData();data.append("file",file.files[0]);const value=await(await fetch("/upload?case="+caseId,{method:"POST",body:data})).json();document.querySelector("#status").textContent=value.passed?"Upload verified":"Upload rejected"};' +
        '</script>'); return;
    }
    if (mode === 'chooser-multi-action') {
      response.end('<!doctype html><meta charset="utf-8"><title>Chooser multi action ' + caseId + '</title>' +
        '<style>body{background:white;color:#152235;font:18px system-ui;margin:20px}button{font:inherit;margin:10px;padding:10px;color:#152235;background:white}[hidden]{display:none!important}</style>' +
        '<h1>Upload entry fixture ready</h1>' +
        '<button id="trigger">Open Upload Picker</button>' +
        '<button id="other">Other Action</button>' +
        '<input id="file" type="file" accept=".txt,.pdf,image/*" hidden>' +
        '<p id="otherStatus">Other:0</p><p id="status">No file transferred</p><script>' +
        'const caseId=' + JSON.stringify(caseId) + ',file=document.querySelector("#file"),trigger=document.querySelector("#trigger"),other=document.querySelector("#other");' +
        'let otherCount=0;function event(v){return fetch("/event?case="+caseId,{method:"POST",body:JSON.stringify(v)})}event({type:"opened"});' +
        'trigger.onclick=e=>{event({type:"click",trusted:e.isTrusted});file.click()};' +
        'other.onclick=()=>{otherCount++;document.querySelector("#otherStatus").textContent="Other:"+otherCount;event({type:"other"})};' +
        'file.onchange=async()=>{event({type:"change",files:[...file.files].map(v=>v.name)});if(!file.files[0])return;const data=new FormData();data.append("file",file.files[0]);const value=await(await fetch("/upload?case="+caseId,{method:"POST",body:data})).json();document.querySelector("#status").textContent=value.passed?"Upload verified":"Upload rejected"};' +
        '</script>'); return;
    }
    if (mode === 'frame-shift-inproc' || mode === 'frame-shift-oopif') {
      const oopif = mode === 'frame-shift-oopif';
      const childUrl = (oopif ? crossOrigin : origin) + '/page?case=' + caseId + '&mode=' + (oopif ? 'custom-text-shift-oopif' : 'custom-text-shift-parent');
      response.end('<!doctype html><style>body{background:white;color:#152235;font:18px system-ui;margin:20px}iframe{display:block;width:700px;height:270px;border:1px solid #777}</style>' +
        '<h1 id="ready">Loading upload entry fixture</h1><div id="spacer" style="height:0"></div><iframe id="frame" src="' + childUrl + '"></iframe>' +
        '<script>const frame=document.querySelector("#frame"),spacer=document.querySelector("#spacer");' +
        'const shift=()=>{spacer.style.height="140px"};' +
        (oopif ? 'window.addEventListener("message",e=>{if(e.data==="shift")shift()});frame.addEventListener("mouseenter",shift,{once:true});' : '') +
        'addEventListener("load",()=>{document.querySelector("#ready").textContent="Upload entry fixture ready"},{once:true});</script>'); return;
    }
    if (frame) { response.end('<!doctype html><style>body{background:white;color:#152235;font:18px system-ui;margin:20px}iframe{width:700px;height:270px;border:1px solid #777}</style><h1 id="ready">Loading upload entry fixture</h1><iframe src="' + crossOrigin + '/page?case=' + caseId + '&mode=' + (mode === 'frame-text' ? 'custom-text' : 'custom') + '"></iframe>' +
      // Ready only after the cross-origin frame has loaded and its document update has invalidated earlier evidence.
      '<script>addEventListener("load",()=>{document.querySelector("#ready").textContent="Upload entry fixture ready"},{once:true})</script>'); return; }
    if (mode === 'delegate' || mode === 'delegate-text') {
      response.end('<!doctype html><style>body{background:white;color:#152235;font:18px system-ui;margin:20px}button{font:inherit;background:white;color:#152235;padding:10px}iframe{display:block;width:650px;height:180px}</style><h1>Upload entry fixture ready</h1>' + (mode === 'delegate-text' ? '<div id="choose"><span>Upload from Device</span></div>' : '<button id="choose">Choose approved fixture</button>') + '<iframe id="child" src="/page?case=' + caseId + '-child&mode=child-hidden"></iframe><script>' +
        'const caseId=' + JSON.stringify(caseId) + ';function event(v){fetch("/event?case="+caseId,{method:"POST",body:JSON.stringify(v)})}event({type:"opened"});document.querySelector("#choose").onclick=e=>{event({type:"click",trusted:e.isTrusted});document.querySelector("#child").contentDocument.querySelector("#file").click()};</script>'); return;
    }
    if (mode === 'popover-device' || mode === 'popover-overlay' || mode === 'popover-menuitem' || mode === 'popover-hide-on-hover') {
      const overlayMode = mode === 'popover-overlay';
      const menuitemMode = mode === 'popover-menuitem';
      const hideHoverMode = mode === 'popover-hide-on-hover';
      const itemMarkup = overlayMode
        ? '<div id="dev" style="position:relative;padding:8px;cursor:pointer"><span>Upload from Device</span><input id="file" type="file" style="position:absolute;inset:0;width:100%;height:100%;opacity:0;cursor:pointer"></div>'
        : menuitemMode
        ? '<ul style="list-style:none;margin:0;padding:0"><li id="dev" role="menuitem" style="padding:8px;cursor:pointer"><span style="pointer-events:none">Upload from Device</span></li></ul>'
        : '<div id="dev" style="padding:8px;cursor:pointer"><span style="pointer-events:none">Upload from Device</span></div>';
      response.end('<!doctype html><meta charset="utf-8"><title>Upload popover fixture ' + caseId + '</title>' +
        '<style>body{background:white;color:#152235;font:18px system-ui;margin:20px}button{font:inherit;background:white;color:#152235;border:1px solid #152235;width:36px;height:36px;cursor:pointer}' +
        '#wrap{position:relative;display:inline-block}#pop{display:none;position:absolute;top:46px;left:0;width:240px;background:white;border:1px solid #152235;padding:8px}' +
        '#dev:hover{font-weight:700;padding-left:14px;background-image:linear-gradient(#eef,#ddf)}[hidden]{display:none!important}</style>' +
        '<h1>Upload entry fixture ready</h1>' +
        '<div id="wrap"><button id="plus">+</button><div id="pop" tabindex="-1">' + itemMarkup + '<div style="padding:8px"><span>Upload from Dropbox</span></div></div></div>' +
        (overlayMode ? '' : '<input id="file" type="file" hidden>') +
        '<p id="status">No file transferred</p><script>' +
        'const caseId=' + JSON.stringify(caseId) + ',overlayMode=' + JSON.stringify(overlayMode) + ',hideHoverMode=' + JSON.stringify(hideHoverMode) + ';' +
        'const wrap=document.querySelector("#wrap"),plus=document.querySelector("#plus"),pop=document.querySelector("#pop"),dev=document.querySelector("#dev"),file=document.querySelector("#file");' +
        'function event(value){return fetch("/event?case="+caseId,{method:"POST",body:JSON.stringify(value)})}event({type:"opened"});' +
        'plus.onclick=()=>{pop.style.display="block";pop.focus()};' +
        'wrap.addEventListener("mouseleave",()=>{pop.style.display="none"});' +
        'pop.addEventListener("focusout",e=>{if(!wrap.contains(e.relatedTarget))pop.style.display="none"});' +
        'if(hideHoverMode)dev.addEventListener("mouseenter",()=>{pop.style.display="none"});' +
        'if(overlayMode){file.onclick=e=>{event({type:"click",trusted:e.isTrusted})}}else{dev.onclick=e=>{event({type:"click",trusted:e.isTrusted});file.click()}}' +
        'file.onchange=async()=>{event({type:"change",files:[...file.files].map(v=>v.name)});if(!file.files[0])return;const data=new FormData();data.append("file",file.files[0]);const value=await(await fetch("/upload?case="+caseId,{method:"POST",body:data})).json();document.querySelector("#status").textContent=value.passed?"Upload verified":"Upload rejected"};' +
        '</script>'); return;
    }
    if (mode === 'oracle-frame') {
      response.end('<!doctype html><style>body{margin:20px;background:white}iframe{width:600px;height:180px;border:0}</style><iframe src="' + crossOrigin + '/page?case=' + caseId + '&mode=oracle-aria-hidden"></iframe>'); return;
    }
    if (mode.startsWith('oracle-')) {
      response.end('<!doctype html><meta charset="utf-8"><style>body{margin:20px;background:white;color:black;font:18px system-ui}#choose{display:inline-block;padding:10px;border:1px solid black}</style><div' + (mode !== 'oracle-text' ? ' aria-hidden="true"' : '') + (mode === 'oracle-hidden' ? ' hidden' : '') + (mode === 'oracle-disabled' ? ' aria-disabled="true"' : '') + '><span id="choose">UPLOAD RESUME</span><input id="file" type="file" hidden><span hidden>HIDDEN_UPLOAD_SENTINEL</span><input value="HIDDEN_EDITABLE_SENTINEL"></div>' + (mode === 'oracle-covered' ? '<div style="position:absolute;left:0;top:0;width:650px;height:150px;background:black"></div>' : '') + '<script>' +
        'const caseId=' + JSON.stringify(caseId) + ';function event(value){return fetch("/event?case="+caseId,{method:"POST",body:JSON.stringify(value)})}event({type:"opened"});document.querySelector("#choose").onclick=e=>{event({type:"click",trusted:e.isTrusted});document.querySelector("#file").click()};document.querySelector("#file").onchange=()=>{const data=new FormData();data.append("file",document.querySelector("#file").files[0]);fetch("/upload?case="+caseId,{method:"POST",body:data})};let changed=false;setInterval(async()=>{const state=await(await fetch("/state?case="+caseId)).json();if(state.changeLabel&&!changed){changed=true;document.querySelector("#choose").textContent="Changed upload entry";event({type:"label"})}},100);</script>'); return;
    }
    const direct = ['direct', 'direct-disabled', 'direct-directory'].includes(mode), label = mode === 'none' ? 'No chooser button' : 'Choose approved fixture';
    // Non-semantic wrapper with delegated click and a real DOM Text node: the
    // SAP-style Device entry is observed as StaticText, not a button or label.
    const textEntry = ['custom-text', 'custom-text-shift-parent', 'custom-text-shift-oopif', 'text-disabled', 'text-none'].includes(mode);
    const entry = textEntry ? '<div id="choose" style="width:600px;height:80px;padding:10px;margin:12px"' + (mode === 'text-disabled' ? ' aria-disabled="true"' : '') + '><span>Upload from Device</span></div>' : '<button id="choose" ' + (mode === 'button-disabled' ? 'disabled' : '') + '>' + label + '</button>';
    const inputFlags = (mode === 'direct-disabled' ? 'disabled ' : '') + (mode.includes('directory') ? 'webkitdirectory directory ' : '') + (mode === 'multiple' ? 'multiple ' : '');
    response.end('<!doctype html><meta charset="utf-8"><title>Upload entry fixture ' + caseId + '</title><style>body{background:white;color:#152235;font:18px system-ui;margin:20px}input,button{font:inherit;margin:12px;padding:10px;color:#152235;background:white}button:hover{filter:brightness(.6);box-shadow:0 2px 6px #0005}body:after{content:"";position:absolute;left:500px;top:10000px;width:20px;height:12px;box-shadow:0 0 1px #888}[hidden]{display:none!important}</style>' +
      '<h1>Upload entry fixture ready</h1><label>' + (direct ? 'Direct upload ' : '') + '<input id="file" type="file" ' + inputFlags + (direct ? '' : 'hidden') + '></label>' + (direct || mode === 'child-hidden' ? '' : entry) + '<p id="status">No file transferred</p>' +
      '<script>const caseId=' + JSON.stringify(caseId) + ',mode=' + JSON.stringify(mode) + ';const file=document.querySelector("#file"),button=document.querySelector("#choose");' +
      'function event(value){return fetch("/event?case="+caseId,{method:"POST",body:JSON.stringify(value)})}event({type:"opened"});' +
      (mode === 'custom-text-shift-parent' ? 'document.addEventListener("mousemove",()=>{const s=window.parent?.document?.querySelector("#spacer");if(s)s.style.height="140px"},{once:true});' : '') +
      (mode === 'custom-text-shift-oopif' ? 'document.addEventListener("mousemove",()=>{window.parent.postMessage("shift","*")},{once:true});' : '') +
      'if(button)button.onclick=e=>{event({type:"click",trusted:e.isTrusted,textHit:e.target.tagName==="SPAN"});if(mode!=="none"&&mode!=="text-none")file.click()};' +
      'file.onchange=async()=>{event({type:"change",files:[...file.files].map(value=>value.name)});if(!file.files[0])return;const data=new FormData();data.append("file",file.files[0]);const value=await(await fetch("/upload?case="+caseId,{method:"POST",body:data})).json();document.querySelector("#status").textContent=value.passed?"Upload verified":"Upload rejected"};' +
      'let changed=false;setInterval(async()=>{const value=await(await fetch("/state?case="+caseId)).json();if(value.changeLabel&&!changed&&button){changed=true;button.textContent="Changed upload button";event({type:"label"})}},100);</script>');
  };
  const start = async () => { const server = http.createServer(handler); await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); }); servers.push(server); return server; };
  const cross = await start(); crossOrigin = 'http://localhost:' + cross.address().port;
  const main = await start(); origin = 'http://127.0.0.1:' + main.address().port;
}
async function prepareBenchmark() {
  if (!benchmarkSource) return;
  const source = await readFile(benchmarkSource), sourceSha256 = sha256(source);
  const copy = resolve(profile, 'browser-benchmark.cjs'); await writeFile(copy, source, { flag: 'wx' });
  assert.equal(sha256(await readFile(copy)), sourceSha256);
  benchmarkModule = createRequire(import.meta.url)(copy); benchmarkRoot = resolve(uploadsRoot, 'benchmark-root');
  benchmarkId = run + '-health';
  const envOwner = process.env.CODEX_THREAD_ID;
  const owner = envOwner && /^[a-zA-Z0-9-]{1,90}$/.test(envOwner) ? envOwner : 'synthetic-' + randomUUID();
  benchmarkServer = await benchmarkModule.main({ action: 'health', root: benchmarkRoot, run: benchmarkId, owner, port: '0' });
  servers.push(benchmarkServer); benchmarkTrial = benchmarkModule.read(benchmarkRoot, benchmarkId).trials[0];
  benchmarkMetadata = { sourceBasename: basename(benchmarkSource), sourceSha256, copiedUnchanged: true, benchmarkId, trialId: benchmarkTrial.id,
    ownerSource: envOwner === owner ? 'CODEX_THREAD_ID' : 'generated_synthetic_identity', syntheticRoot: benchmarkRoot };
}
async function fullHealthFlow(candidate = { id: benchmarkId, trial: benchmarkTrial, server: benchmarkServer, worker }) {
  const { id, trial, server, worker: owner } = candidate;
  const current = () => benchmarkModule.read(benchmarkRoot, id).trials.find(item => item.id === trial.id);
  const expected = benchmarkModule.expectedForTrial(trial), health = trial.kind === 'health';
  const url = 'http://127.0.0.1:' + server.address().port + '/trial/' + trial.id;
  const tab = { ...owner, ...await tool('tab_create', { ...workerScope(owner), url }) };
  const started = Date.now(); await until(async () => (await observe(tab)).nodes.some(node => node.name === (health ? 'Browser health check' : 'Synthetic application')));
  await act(tab, 'Import sample résumé');
  await until(() => current().fixture?.imported);
  await act(tab, 'Full name', 'fill', { text: expected.name });
  if (!health) {
    await act(tab, 'Email', 'fill', { text: expected.email });
    await act(tab, 'Start date', 'fill', { text: expected.start });
  }
  await act(tab, 'Location', 'select', { values: [expected.city] });
  if (!health) { await act(tab, 'Add employment'); await act(tab, 'Employer 2', 'fill', { text: expected.employers[1] }); }
  await act(tab, 'Add employment'); await act(tab, 'Employer 3', 'fill', { text: expected.employers[2] });
  await act(tab, 'I checked this synthetic information', 'check', { checked: true });
  await act(tab, 'Continue to attachments');
  const files = await tool('files', { ...workerScope(owner), folderId, limit: 1000 });
  const fixture = files.files.find(item => item.name === 'benchmark-fixture.txt' && item.relativePath?.includes(id)); assert(fixture);
  const selected = await upload(tab, 'Choose fixture file', fixture.fileId);
  if (baseline) { assert(selected.isError); assert.equal(selected.structuredContent?.error?.code, 'not_file_input'); }
  else { assert(!selected.isError, JSON.stringify(selected.structuredContent)); selectionFacts(selected, 'dispatched', 'selected'); await until(() => current().fixture?.uploaded); }
  await act(tab, 'Prepare review');
  await until(() => current().fixture?.delayed);
  await act(tab, 'Review application');
  const graded = await until(() => { const value = current(); return value.grade ? value : false; });
  assert.equal(graded.fixture?.final_activated ?? false, false); assert.equal(graded.fixture?.untrusted ?? false, false);
  assert(!graded.observations.some(item => item.type === 'final'));
  if (baseline) { assert.equal(graded.grade.passed, false); assert.deepEqual(graded.grade.failures, ['upload_transfer']); }
  else { assert.equal(graded.grade.passed, true); assert.deepEqual(graded.grade.failures, []); }
  await target(tab, 'Final Submit — do not activate', 'BUTTON'); // Evidence only, never activate.
  const browserActionMs = Date.now() - started;
  return { kind: trial.kind, fixtureSourceUnmodified: true, grade: graded.grade, finalSubmitUntouched: true, trustedActivation: true,
    import: graded.fixture.imported, uploaded: !!graded.fixture.uploaded, delayed: graded.fixture.delayed, browserActionMs,
    targetMs: 90000, overrun: browserActionMs > 90000, comparisonOrReusableCertification: false };
}
async function matrixCandidate(kind, index, owner = worker) {
  const id = run + '-' + kind + '-' + index;
  await benchmarkModule.main({ action: 'init', root: benchmarkRoot, run: id, owner: 'synthetic-' + randomUUID() });
  const trial = benchmarkModule.addTrial(benchmarkRoot, id, ...benchmarkModule.CONFIGS[0], kind);
  benchmarkModule.register(benchmarkRoot, id, trial.id, 'synthetic-' + randomUUID());
  const server = await benchmarkModule.main({ action: 'serve', root: benchmarkRoot, run: id, trial: trial.id, port: '0' });
  servers.push(server);return { id, trial, server, worker: owner };
}

if (process.platform !== 'win32') throw new Error('Upload regression requires Windows.');
if (await alivePipe()) throw new Error('Randomly allocated private pipe already exists.');
try {
  await mkdir(profile, { recursive: true }); await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_AUTH_FIXTURE\n');
  const seeded = JSON.parse((await runFile(resolve(buildRoot, 'Release/vault_fixture_seed.exe'), [profile], { windowsHide: true })).stdout.trim()); folderId = seeded.folderId;
  await mkdir(resolve(uploadsRoot, 'nested'), { recursive: true });
  for (let index = 0; index < 7; index++) await writeFile(resolve(uploadsRoot, 'nested', `page-${index}.txt`), `Synthetic discovery file ${index}\n`);
  const identity = { clientId: 'upload_' + randomBytes(8).toString('hex'), token: randomBytes(32).toString('hex'), pipe };
  const noUploadsIdentity = { clientId: 'noupload_' + randomBytes(8).toString('hex'), token: randomBytes(32).toString('hex'), pipe };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({
    version: 2,
    clients: [
      { id: identity.clientId, name: 'Synthetic upload client', tokenHash: sha256(identity.token), policy: { interaction: true, uploads: true, downloads: true, savedAccounts: true, automaticWorkspaces: true, maxWorkers: 16, maxAutomaticWorkspaces: 16 } },
      { id: noUploadsIdentity.clientId, name: 'Synthetic no-upload client', tokenHash: sha256(noUploadsIdentity.token), policy: { interaction: true, uploads: false, downloads: false, savedAccounts: false, automaticWorkspaces: true, maxWorkers: 4, maxAutomaticWorkspaces: 4 } },
    ],
    workspaces: [{
      id: workspaceId,
      clients: [identity.clientId, noUploadsIdentity.clientId],
      access: {
        [identity.clientId]: { interaction: true, uploads: true, downloads: true, savedAccounts: true },
        [noUploadsIdentity.clientId]: { interaction: true, uploads: false, downloads: false, savedAccounts: false },
      },
    }],
    accountGrants: [],
    operations: [],
  }));
  await writePrivateConfig(resolve(profile, 'client-config.json'), identity);
  await writePrivateConfig(resolve(profile, 'no-uploads-client-config.json'), noUploadsIdentity);
  await prepareBenchmark(); await startFixtures(); applicationDllSha256 = await hashDll(); await launch();
  if (oracleObservation) {
    const stableRead = async (method, args) => until(async () => {
      const response = await raw(method, args);
      // Frame attachment/layout can invalidate a read; retry fresh reads only.
      // Mutations below are never retried by this fixture.
      if (response.isError && ['privacy_guard_initializing', 'page_changed', 'inspection_changed'].includes(response.structuredContent?.error?.code)) return false;
      assert(!response.isError, JSON.stringify(response.structuredContent)); return response;
    }, 5000, 'Stable synthetic upload observation');
    const inspectUpload = async mode => {
      const tab = { ...worker, ...await tool('tab_create', { ...workerScope(worker), url: origin + '/page?case=' + mode + '&mode=' + mode }) };
      await until(() => fixtureStates.get(mode)?.opened);
      const inspected = await stableRead('inspect', { ...scope(tab), maxNodes: 1000 });
      const observation = inspected.structuredContent;
      const node = observation.nodes.find(node => node.name === 'UPLOAD RESUME' && node.ref);
      await writeFile(resolve(profile, mode + '-observation.json'), JSON.stringify({ nodes: observation.nodes, coverage: observation.coverage, frames: observation.frames, visibility: observation.visibility }, null, 2));
      const screenshot = inspected.content.find(item => item.type === 'image');
      assert(screenshot, 'Synthetic inspection did not return a screenshot');
      await writeFile(resolve(profile, mode + '.png'), Buffer.from(screenshot.data, 'base64'));
      assert(!JSON.stringify(observation).includes('HIDDEN_'), 'Hidden text or editable value entered observation');
      return { tab, observation, node };
    };
    for (const mode of ['oracle-text', 'oracle-aria-hidden', 'oracle-frame']) await check('Visible UPLOAD RESUME has an upload ref: ' + mode, async () => {
      const { tab, observation, node } = await inspectUpload(mode);
      assert(node, 'Screenshot-visible UPLOAD RESUME has no ref: ' + JSON.stringify({ nodes: observation.nodes.length, coverage: observation.coverage, omitted: observation.visibility?.omitted }));
      if (mode !== 'oracle-text') { assert.equal(node.provenance, 'chromium-rendered-text-node'); assert.equal(node.roleProvenance, 'native-dom-text-type'); }
      for (const method of ['observe', 'inspect']) {
        const queried = (await stableRead(method, { ...scope(tab), query: 'UPLOAD RESUME' })).structuredContent;
        assert.equal(queried.nodes.filter(item => item.name === 'UPLOAD RESUME' && item.ref).length, 1, 'Targeted query lost or duplicated the rendered upload entry');
      }
      const current = await tool('observe', { ...scope(tab), query: 'UPLOAD RESUME' }), currentNode = current.nodes.find(item => item.name === 'UPLOAD RESUME' && item.ref);
      const response = await raw('upload', { ...mutation(tab), observationId: current.observationId, elementRef: currentNode.ref, fileId });
      assert(!response.isError, JSON.stringify(response.structuredContent)); selectionFacts(response, 'dispatched', 'selected');
      await until(() => fixtureStates.get(mode).uploaded);
      assert.equal(fixtureStates.get(mode).uploads, 1); assert.equal(fixtureStates.get(mode).clicks, 1);
      assert.deepEqual(fixtureStates.get(mode).trusted, [true]);
      return { verifiedRef: true, trustedClicks: fixtureStates.get(mode).trusted, exactApprovedContentsReceived: true };
    });
    for (const mode of ['oracle-hidden', 'oracle-covered']) await check('Unrendered upload text remains withheld: ' + mode, async () => {
      const { node, observation } = await inspectUpload(mode); assert.equal(node, undefined); assert(!JSON.stringify(observation).includes('UPLOAD RESUME'));
      assert.equal(fixtureStates.get(mode).clicks, 0); assert.equal(fixtureStates.get(mode).uploads, 0); return { withheld: true, uploads: 0 };
    });
    await check('Rendered-only upload text preserves disabled ancestor checks', async () => {
      const { tab, observation, node } = await inspectUpload('oracle-disabled'); assert(node);
      const response = await raw('upload', { ...mutation(tab), observationId: observation.observationId, elementRef: node.ref, fileId });
      assert(response.isError); assert.equal(response.structuredContent.error.code, 'unsupported_upload_entry'); selectionFacts(response, 'not_attempted', 'not_selected');
      assert.equal(fixtureStates.get('oracle-disabled').clicks, 0); return { deniedBeforeActivation: true };
    });
    await check('Rendered-only upload text revalidates its label before activation', async () => {
      const { tab, observation, node } = await inspectUpload('oracle-stale'); assert(node);
      fixtureStates.get('oracle-stale').changeLabel = true; await until(() => fixtureStates.get('oracle-stale').labelChanged);
      const response = await raw('upload', { ...mutation(tab), observationId: observation.observationId, elementRef: node.ref, fileId });
      assert(response.isError); assert.equal(response.structuredContent.error.code, 'stale_element'); selectionFacts(response, 'not_attempted', 'not_selected');
      assert.equal(fixtureStates.get('oracle-stale').clicks, 0); assert.equal(fixtureStates.get('oracle-stale').uploads, 0); return { staleRejectedBeforeActivation: true };
    });
  } else {
  await check('File discovery retains truncation, later pages and scoped relative-path filters', async () => {
    const names = [], args = { ...workerScope(worker), folderId, limit: 2 };
    let cursor, pages = 0;
    do {
      const page = await tool('files', { ...args, ...(cursor ? { cursor } : {}) });
      assert.equal(typeof page.truncated, 'boolean'); assert.equal(page.scanIncomplete, false);
      assert(page.files.length <= 2); names.push(...page.files.map(file => file.relativePath)); pages++;
      assert(pages < 3000); cursor = page.nextCursor;
      if (cursor) {
        assert.equal(page.truncated, true);
        const mismatch = await raw('files', { ...args, agentSessionId: otherWorker.agentSessionId, cursor });
        assert.equal(mismatch.isError, true); assert.equal(mismatch.structuredContent.error.code, 'invalid_cursor');
      } else assert.equal(page.truncated, false);
    } while (cursor);
    assert(names.length >= 8); assert.equal(new Set(names).size, names.length); assert(pages > 1);
    for (let index = 0; index < 7; index++) assert(names.some(name => name.replaceAll('\\', '/') === `nested/page-${index}.txt`));
    const filtered = await tool('files', { ...args, limit: 1, query: 'NESTED/PAGE-6.TXT' });
    assert.equal(filtered.files.length, 1); assert.equal(filtered.files[0].name, 'page-6.txt'); assert.equal(filtered.truncated, false);
    if (!baseline) {
      const relativePath = 'deep/'.repeat(10) + 'approved-report.txt';
      await mkdir(resolve(uploadsRoot, 'deep/'.repeat(10)), { recursive: true });
      await writeFile(resolve(uploadsRoot, relativePath), 'Xenon synthetic approved upload.\n');
      const incomplete = await tool('files', { ...args, query: relativePath });
      assert.deepEqual(incomplete.files, []); assert.equal(incomplete.scanIncomplete, true); assert.equal(incomplete.nextCursor, null);
      const exact = await tool('files', { ...args, relativePath });
      assert.equal(exact.files.length, 1); assert.equal(exact.files[0].relativePath.replaceAll('\\', '/'), relativePath);
      assert.equal(exact.scanIncomplete, false); assert.equal(exact.truncated, false); assert.equal(exact.nextCursor, null);
      const denied = await raw('files', { ...workerScope(otherWorker), folderId, relativePath });
      assert.equal(denied.isError, true);
      // All following uploads verify the contents of the directly resolved file.
      fileId = exact.files[0].fileId;
    }
    return { pages, uniqueFiles: names.length, laterPageFilterVerified: true, directLookupBeyondDepthVerified: !baseline };
  });
  await check('Visible direct file input transfers only an approved opaque file', async () => {
    const tab = await open('direct', 'direct'), response = await upload(tab, 'Direct upload', fileId, 'INPUT');
    assert(!response.isError, JSON.stringify(response.structuredContent)); selectionFacts(response, 'not_attempted', 'selected'); await until(() => fixtureStates.get('direct').uploaded);
    assert.equal(fixtureStates.get('direct').uploads, 1); return { uploadVerified: true, uploads: 1 };
  });
  for (const [caseId, mode] of [['custom', 'custom'], ['cross-frame', 'frame']]) await check((baseline ? 'Baseline reproduces unsupported ' : 'Approved file follows trusted ') + caseId + ' upload entry', async () => {
    const tab = await open(caseId, mode), response = await upload(tab, 'Choose approved fixture');
    if (baseline) { assert(response.isError); assert.equal(response.structuredContent?.error?.code, 'not_file_input'); assert.equal(fixtureStates.get(caseId).uploads, 0); }
    else { assert(!response.isError, JSON.stringify(response.structuredContent)); selectionFacts(response, 'dispatched', 'selected'); await until(() => fixtureStates.get(caseId).uploaded && fixtureStates.get(caseId).clicks === 1); assert.equal(fixtureStates.get(caseId).clicks, 1); assert.deepEqual(fixtureStates.get(caseId).trusted, [true]); assert.equal(fixtureStates.get(caseId).uploads, 1); }
    return { reproducedUnsupportedEntry: baseline, uploaded: fixtureStates.get(caseId).uploaded, crossOriginFrame: mode === 'frame' };
  });
  if (!baseline) await check('A brightness-styled hover preserves the approved upload entry', async () => {
    const tab = await open('hover-entry', 'custom'), { observation, node } = await target(tab, 'Choose approved fixture', 'BUTTON');
    await tool('interact', { ...mutation(tab), observationId: observation.observationId, action: 'hover', elementRef: node.ref });
    const response = await upload(tab, 'Choose approved fixture');
    assert(!response.isError, JSON.stringify(response.structuredContent)); selectionFacts(response, 'dispatched', 'selected');
    await until(() => fixtureStates.get('hover-entry').uploaded && fixtureStates.get('hover-entry').clicks === 1);
    assert.equal(fixtureStates.get('hover-entry').uploads, 1);
    return { hoverSucceeded: true, freshReferenceUsable: true, uploads: 1 };
  });
  if (!baseline) {
    for (const mode of ['custom-text', 'frame-text']) await check('Observed Device text opens its exact chooser: ' + mode, async () => {
      const tab = await open(mode, mode), { observation, node } = await target(tab, 'Upload from Device');
      assert.equal(node.role, 'StaticText'); assert.equal(node.visibilityStatus, 'verified');
      const response = await raw('upload', { ...mutation(tab), observationId: observation.observationId, elementRef: node.ref, fileId });
      assert(!response.isError, JSON.stringify(response.structuredContent)); selectionFacts(response, 'dispatched', 'selected');
      await until(() => fixtureStates.get(mode).uploaded && fixtureStates.get(mode).changes === 1 && fixtureStates.get(mode).clicks === 1);
      assert.deepEqual(fixtureStates.get(mode).selectedFiles, ['approved-report.txt']);
      assert.deepEqual(fixtureStates.get(mode).textHits, [true], 'Click must hit the text span, not the larger container');
      assert.deepEqual(fixtureStates.get(mode).trusted, [true]); assert.equal(fixtureStates.get(mode).uploads, 1);
      return { observedRole: node.role, exactApprovedContentsReceived: true, trustedClicks: 1, crossOriginFrame: mode === 'frame-text' };
    });
    await check('Changed Device text rejects its stale reference before activation', async () => {
      const tab = await open('stale-text', 'custom-text'), { observation, node } = await target(tab, 'Upload from Device');
      fixtureStates.get('stale-text').changeLabel = true; await until(() => fixtureStates.get('stale-text').labelChanged);
      const response = await raw('upload', { ...mutation(tab), observationId: observation.observationId, elementRef: node.ref, fileId });
      assert(response.isError); assert.equal(response.structuredContent?.error?.code, 'stale_element');
      selectionFacts(response, 'not_attempted', 'not_selected');
      assert.equal(fixtureStates.get('stale-text').clicks, 0); assert.equal(fixtureStates.get('stale-text').uploads, 0);
      return { rejectedStaleText: true, clicks: 0, uploads: 0 };
    });
    await check('Device text in a disabled container is denied before activation', async () => {
      const tab = await open('text-disabled', 'text-disabled'), response = await upload(tab, 'Upload from Device', fileId, null);
      assert(response.isError); assert.equal(response.structuredContent?.error?.code, 'unsupported_upload_entry');
      selectionFacts(response, 'not_attempted', 'not_selected');
      assert.equal(fixtureStates.get('text-disabled').clicks, 0); assert.equal(fixtureStates.get('text-disabled').uploads, 0);
      return { disabledAncestorRejected: true, clicks: 0, uploads: 0 };
    });
    await check('Ordinary observed text without a chooser never receives a file', async () => {
      const tab = await open('text-none', 'text-none'), response = await upload(tab, 'Upload from Device', fileId, null);
      assert(response.isError); assert.equal(response.structuredContent?.error?.code, 'file_chooser_not_opened');
      selectionFacts(response, 'dispatched', 'not_selected');
      await until(() => fixtureStates.get('text-none').clicks === 1);
      assert.equal(fixtureStates.get('text-none').uploads, 0); assert.equal(fixtureStates.get('text-none').changes, 0);
      return { activatedOnce: true, uploads: 0, changes: 0 };
    });
    await check('Device text cannot delegate its approved file to another frame', async () => {
      const tab = await open('delegate-text', 'delegate-text'); await until(() => fixtureStates.get('delegate-text-child')?.opened);
      const response = await upload(tab, 'Upload from Device', fileId, null); assert(response.isError);
      selectionFacts(response, 'dispatched', 'not_selected');
      await until(() => fixtureStates.get('delegate-text').clicks === 1);
      assert.equal(fixtureStates.get('delegate-text-child').uploads, 0); assert.equal(fixtureStates.get('delegate-text-child').changes, 0);
      return { rejected: true, code: response.structuredContent?.error?.code, childReceivedFiles: false };
    });
  }
  await check('A visible button without a file chooser cannot receive a file', async () => {
    const tab = await open('no-chooser', 'none'), response = await upload(tab, 'No chooser button'); assert(response.isError);
    assert.equal(fixtureStates.get('no-chooser').uploads, 0);
    if (!baseline) { selectionFacts(response, 'dispatched', 'not_selected'); await until(() => fixtureStates.get('no-chooser').clicks === 1); assert.deepEqual(fixtureStates.get('no-chooser').trusted, [true]); }
    return { denied: true, code: response.structuredContent?.error?.code, activatedOnce: !baseline, uploads: 0 };
  });
  await check('Invalid and cross-workspace grants are denied before entry activation', async () => {
    for (const [caseId, owner, grant, textEntry] of [['invalid-grant', worker, 'not_a_granted_file', false], ['cross-workspace', otherWorker, fileId, false], ['invalid-text-grant', worker, 'not_a_granted_file', true], ['cross-workspace-text', otherWorker, fileId, true]]) {
      const tab = await open(caseId, textEntry ? 'custom-text' : 'custom', owner), response = await upload(tab, textEntry ? 'Upload from Device' : 'Choose approved fixture', grant, textEntry ? null : 'BUTTON');
      assert(response.isError); assert.equal(response.structuredContent?.error?.code, 'file_denied');
      selectionFacts(response, 'not_attempted', 'not_selected');
      await sleep(150); // Let any incorrectly dispatched website events arrive.
      assert.equal(fixtureStates.get(caseId).clicks, 0); assert.equal(fixtureStates.get(caseId).uploads, 0);
    }
    return { denials: 4, activations: 0, uploads: 0 };
  });
  await check('A changed visible upload entry rejects its stale reference before input', async () => {
    const tab = await open('stale-entry', 'custom'), { observation, node } = await target(tab, 'Choose approved fixture', 'BUTTON');
    fixtureStates.get('stale-entry').changeLabel = true; await until(() => fixtureStates.get('stale-entry').labelChanged);
    const response = await raw('upload', { ...mutation(tab), observationId: observation.observationId, elementRef: node.ref, fileId });
    assert(response.isError); assert.equal(response.structuredContent?.error?.code, 'stale_element');
    selectionFacts(response, 'not_attempted', 'not_selected');
    await sleep(150);
    assert.equal(fixtureStates.get('stale-entry').clicks, 0); assert.equal(fixtureStates.get('stale-entry').uploads, 0);
    return { deniedAsStale: true, clicks: 0, uploads: 0 };
  });
  if (!baseline) {
    await check('A multiple-file chooser receives only the one approved file', async () => {
      const tab = await open('multiple', 'multiple'), response = await upload(tab, 'Choose approved fixture');
      assert(!response.isError, JSON.stringify(response.structuredContent)); selectionFacts(response, 'dispatched', 'selected'); await until(() => fixtureStates.get('multiple').uploaded && fixtureStates.get('multiple').selectedFiles?.length === 1 && fixtureStates.get('multiple').clicks === 1);
      assert.deepEqual(fixtureStates.get('multiple').selectedFiles, ['approved-report.txt']); assert.deepEqual(fixtureStates.get('multiple').trusted, [true]);
      assert.equal(fixtureStates.get('multiple').uploads, 1); return { selectedFileCount: 1, uploadVerified: true };
    });
    await check('Disabled controls and directory pickers cannot transfer files', async () => {
      const details = [];
      for (const mode of ['direct-disabled', 'button-disabled', 'direct-directory', 'button-directory']) {
        const tab = await open(mode, mode), direct = mode.startsWith('direct');
        const response = await upload(tab, direct ? 'Direct upload' : 'Choose approved fixture', fileId, direct ? 'INPUT' : 'BUTTON');
        assert(response.isError, mode + ' unexpectedly accepted a file');
        selectionFacts(response, mode === 'button-directory' ? 'dispatched' : 'not_attempted', 'not_selected');
        await sleep(150);
        assert.equal(fixtureStates.get(mode).uploads, 0); assert.equal(fixtureStates.get(mode).changes, 0);
        if (mode !== 'button-directory') assert.equal(fixtureStates.get(mode).clicks, 0);
        details.push({ mode, denied: true, code: response.structuredContent?.error?.code });
      }
      return { cases: details, transfers: 0 };
    });
    await check('A parent entry cannot delegate its file grant to a different frame', async () => {
      const tab = await open('delegation', 'delegate'); await until(() => fixtureStates.get('delegation-child')?.opened);
      const response = await upload(tab, 'Choose approved fixture'); assert(response.isError);
      selectionFacts(response, 'dispatched', 'not_selected');
      await until(() => fixtureStates.get('delegation').clicks === 1); assert.deepEqual(fixtureStates.get('delegation').trusted, [true]);
      await sleep(150);
      assert.equal(fixtureStates.get('delegation-child').uploads, 0); assert.equal(fixtureStates.get('delegation-child').changes, 0);
      return { denied: true, code: response.structuredContent?.error?.code, parentActivatedOnce: true, childReceivedFiles: false };
    });
    await check('Transient + popover with pointer-events:none StaticText, hover shift, mouseleave and blur guards uploads cleanly', async () => {
      const tab = await open('popover-device', 'popover-device');
      await act(tab, '+', 'click', {}, 'BUTTON');
      const compact = await until(async () => {
        const res = await raw('observe', { ...scope(tab), maxNodes: 500, compact: true });
        if (res.isError) return false;
        return res.structuredContent.nodes.some(n => n.name === 'Upload from Device' && n.ref) ? res.structuredContent : false;
      });
      const entry = compact.nodes.find(n => n.name === 'Upload from Device' && n.ref);
      assert.equal(entry.role, 'StaticText');
      const response = await raw('upload', { ...mutation(tab), observationId: compact.observationId, elementRef: entry.ref, fileId });
      assert(!response.isError, JSON.stringify(response.structuredContent));
      selectionFacts(response, 'dispatched', 'selected');
      await until(() => fixtureStates.get('popover-device').uploaded && fixtureStates.get('popover-device').changes === 1 && fixtureStates.get('popover-device').clicks === 1);
      assert.deepEqual(fixtureStates.get('popover-device').selectedFiles, ['approved-report.txt']);
      assert.deepEqual(fixtureStates.get('popover-device').trusted, [true]);
      assert.equal(fixtureStates.get('popover-device').uploads, 1);
      return { compactStaticTextPreserved: true, trustedClicks: 1, uploadVerified: true };
    });
    await check('Transient + popover with transparent file-input overlay uploads via xenon_upload while ordinary click stays blocked', async () => {
      const tab = await open('popover-overlay', 'popover-overlay');
      await act(tab, '+', 'click', {}, 'BUTTON');
      const obs1 = await until(async () => {
        const res = await raw('observe', { ...scope(tab), maxNodes: 500, compact: true });
        if (res.isError) return false;
        return res.structuredContent.nodes.some(n => n.name === 'Upload from Device' && n.ref) ? res.structuredContent : false;
      });
      const entry1 = obs1.nodes.find(n => n.name === 'Upload from Device' && n.ref);
      const blockedClick = await raw('interact', { ...mutation(tab), observationId: obs1.observationId, action: 'click', elementRef: entry1.ref });
      assert.equal(blockedClick.isError, true, 'Ordinary click must refuse transparent file input overlay');
      assert.equal(blockedClick.structuredContent?.error?.code, 'not_actionable');
      assert.equal(blockedClick.structuredContent?.error?.reason, 'occluded');
      assert.equal(fixtureStates.get('popover-overlay').uploads, 0);

      const obs2 = await observe(tab);
      const entry2 = obs2.nodes.find(n => n.name === 'Upload from Device' && n.ref);
      assert(entry2);
      const response = await raw('upload', { ...mutation(tab), observationId: obs2.observationId, elementRef: entry2.ref, fileId });
      assert(!response.isError, JSON.stringify(response.structuredContent));
      selectionFacts(response, 'dispatched', 'selected');
      await until(() => fixtureStates.get('popover-overlay').uploaded && fixtureStates.get('popover-overlay').changes === 1);
      assert.deepEqual(fixtureStates.get('popover-overlay').selectedFiles, ['approved-report.txt']);
      assert.deepEqual(fixtureStates.get('popover-overlay').trusted, [true]);
      assert.equal(fixtureStates.get('popover-overlay').uploads, 1);
      return { ordinaryClickBlocked: true, uploadVerified: true };
    });
    await check('Compact semantic menuitem popover uploads cleanly and mouseenter-dismissed popover fails closed with allowlisted reason', async () => {
      const tab = await open('popover-menuitem', 'popover-menuitem');
      await act(tab, '+', 'click', {}, 'BUTTON');
      const compact = await until(async () => {
        const res = await raw('observe', { ...scope(tab), maxNodes: 500, compact: true });
        if (res.isError) return false;
        return res.structuredContent.nodes.some(n => n.name === 'Upload from Device' && n.ref) ? res.structuredContent : false;
      });
      const matches = compact.nodes.filter(n => n.name === 'Upload from Device');
      assert.equal(matches.length, 1, 'Compact mode should deduplicate duplicate StaticText inside role=menuitem: ' + JSON.stringify(matches));
      assert.equal(matches[0].role, 'menuitem');
      const response = await raw('upload', { ...mutation(tab), observationId: compact.observationId, elementRef: matches[0].ref, fileId });
      assert(!response.isError, JSON.stringify(response.structuredContent));
      selectionFacts(response, 'dispatched', 'selected');
      await until(() => fixtureStates.get('popover-menuitem').uploaded && fixtureStates.get('popover-menuitem').clicks === 1);
      assert.equal(fixtureStates.get('popover-menuitem').uploads, 1);

      const hostileTab = await open('popover-hide-on-hover', 'popover-hide-on-hover');
      await act(hostileTab, '+', 'click', {}, 'BUTTON');
      const hostileObs = await until(async () => {
        const res = await raw('observe', { ...scope(hostileTab), maxNodes: 500, compact: true });
        if (res.isError) return false;
        return res.structuredContent.nodes.some(n => n.name === 'Upload from Device' && n.ref) ? res.structuredContent : false;
      });
      const hostileEntry = hostileObs.nodes.find(n => n.name === 'Upload from Device' && n.ref);
      const denied = await raw('upload', { ...mutation(hostileTab), observationId: hostileObs.observationId, elementRef: hostileEntry.ref, fileId });
      assert.equal(denied.isError, true);
      assert.equal(denied.structuredContent?.error?.code, 'upload_activation_failed');
      selectionFacts(denied, 'attempted', 'not_selected');
      assert(['not_visible', 'detached', 'occluded', 'layout_moving'].includes(denied.structuredContent?.error?.reason), JSON.stringify(denied.structuredContent));
      await sleep(150);
      assert.equal(fixtureStates.get('popover-hide-on-hover').clicks, 0);
      assert.equal(fixtureStates.get('popover-hide-on-hover').uploads, 0);
      return { menuitemDeduplicatedAndUploaded: true, hostileHoverDeniedReason: denied.structuredContent.error.reason, hostileClicks: 0 };
    });
    await check('Iframe upload entry shifted by parent layout change on mouseMoved fails closed with stale_geometry', async () => {
      const modes = [];
      for (const mode of ['frame-shift-inproc', 'frame-shift-oopif']) {
        const tab = await open(mode, mode);
        const { observation, node } = await target(tab, 'Upload from Device');
        const response = await raw('upload', { ...mutation(tab), observationId: observation.observationId, elementRef: node.ref, fileId });
        assert.equal(response.isError, true, mode + ' unexpectedly succeeded after iframe shifted');
        assert.equal(response.structuredContent?.error?.code, 'upload_activation_failed', JSON.stringify(response.structuredContent));
        assert.equal(response.structuredContent?.error?.reason, 'stale_geometry', JSON.stringify(response.structuredContent));
        selectionFacts(response, 'attempted', 'not_selected');
        await sleep(150);
        assert.equal(fixtureStates.get(mode).clicks, 0);
        assert.equal(fixtureStates.get(mode).uploads, 0);
        modes.push({ mode, reason: response.structuredContent.error.reason, clicks: 0, uploads: 0 });
      }
      return { cases: modes };
    });
    await check('Visual coordinate click on SVG dropzone intercepts file chooser, survives xenon_files read, and fulfills via xenon_upload', async () => {
      const tab = await open('svg-dropzone', 'svg-dropzone');
      const inspected = await until(async () => {
        const res = await raw('inspect', { ...scope(tab), compact: true });
        if (res.isError && ['privacy_guard_initializing', 'inspection_changed', 'page_changed'].includes(res.structuredContent?.error?.code)) return false;
        assert(!res.isError, JSON.stringify(res.structuredContent));
        return res.structuredContent;
      });
      const clicked = await tool('interact', {
        ...mutation(tab), observationId: inspected.observationId, action: 'click',
        x: 150, y: 165, observeAfter: { compact: true },
      });
      assert.equal(clicked.status, 'dispatched');
      assert(clicked.fileChooser && clicked.fileChooser.status === 'pending', JSON.stringify(clicked));
      assert.equal(clicked.fileChooser.mode, 'open');
      assert(typeof clicked.fileChooser.chooserId === 'string' && clicked.fileChooser.chooserId.length > 0);
      assert(Array.isArray(clicked.fileChooser.acceptFilters));
      assert(clicked.fileChooser.acceptFilters.includes('.txt') || clicked.fileChooser.acceptFilters.includes('text/plain'), JSON.stringify(clicked.fileChooser.acceptFilters));
      assert(!clicked.fileChooser.acceptFilters.some(f => f.includes(' ') || f.includes('\\')), 'Unsafe acceptFilters leaked: ' + JSON.stringify(clicked.fileChooser.acceptFilters));
      assert(clicked.observation && clicked.observation.pendingFileChooser?.chooserId === clicked.fileChooser.chooserId, JSON.stringify(clicked));

      const status = await tool('control_status', scope(tab));
      assert.equal(status.pendingFileChooser?.chooserId, clicked.fileChooser.chooserId);
      // Read-only xenon_files lookup must NOT cancel the pending chooser.
      const listed = await tool('files', { ...workerScope(worker), folderId, limit: 10 });
      assert(listed.files.length > 0);

      const fulfilled = await raw('upload', { ...mutation(tab), chooserId: clicked.fileChooser.chooserId, fileId });
      assert(!fulfilled.isError, JSON.stringify(fulfilled.structuredContent));
      assert.equal(fulfilled.structuredContent.status, 'selected');
      selectionFacts(fulfilled, 'not_attempted', 'selected');
      await until(() => fixtureStates.get('svg-dropzone').uploaded && fixtureStates.get('svg-dropzone').changes === 1);
      assert.deepEqual(fixtureStates.get('svg-dropzone').selectedFiles, ['approved-report.txt']);
      const afterStatus = await tool('control_status', scope(tab));
      assert.equal(afterStatus.pendingFileChooser, undefined);
      return { interceptedCoordinateDropzone: true, acceptFilters: clicked.fileChooser.acceptFilters, uploads: 1 };
    });
    await check('Element click and Enter key intercept file chooser and xenon_upload cancel=true dismisses cleanly', async () => {
      const tab = await open('chooser-cancel', 'chooser-multi-action');
      const { observation, node } = await target(tab, 'Open Upload Picker', 'BUTTON');
      const clicked = await tool('interact', { ...mutation(tab), observationId: observation.observationId, action: 'click', elementRef: node.ref });
      assert.equal(clicked.fileChooser?.status, 'pending', JSON.stringify(clicked));
      const canceled = await raw('upload', { ...mutation(tab), chooserId: clicked.fileChooser.chooserId, cancel: true });
      assert(!canceled.isError, JSON.stringify(canceled.structuredContent));
      assert.equal(canceled.structuredContent.status, 'canceled');
      selectionFacts(canceled, 'not_attempted', 'not_selected');
      await sleep(120);
      assert.equal(fixtureStates.get('chooser-cancel').changes, 0);
      assert.equal(fixtureStates.get('chooser-cancel').uploads, 0);
      const status = await tool('control_status', scope(tab));
      assert.equal(status.pendingFileChooser, undefined);

      // Re-trigger via Enter key on the still-focused button and cancel cleanly too.
      const obsForKey = await observe(tab);
      const keyed = await tool('interact', { ...mutation(tab), observationId: obsForKey.observationId, action: 'key', key: 'Enter' });
      assert.equal(keyed.fileChooser?.status, 'pending', JSON.stringify(keyed));
      const canceledKey = await raw('upload', { ...mutation(tab), chooserId: keyed.fileChooser.chooserId, cancel: true });
      assert(!canceledKey.isError, JSON.stringify(canceledKey.structuredContent));

      // Tab remains fully interactive after cancellation.
      await act(tab, 'Other Action', 'click', {}, 'BUTTON');
      await until(() => fixtureStates.get('chooser-cancel').otherClicks === 1);
      return { clickCanceled: true, enterKeyInterceptedAndCanceled: true, subsequentClickWorked: true };
    });
    await check('Directory chooser opened by xenon_interact(click) is immediately rejected without OS dialog', async () => {
      const tab = await open('chooser-dir', 'button-directory');
      const { observation, node } = await target(tab, 'Choose approved fixture', 'BUTTON');
      const clicked = await tool('interact', { ...mutation(tab), observationId: observation.observationId, action: 'click', elementRef: node.ref });
      assert.equal(clicked.status, 'dispatched');
      assert.equal(clicked.fileChooser?.status, 'rejected', JSON.stringify(clicked));
      assert.equal(clicked.fileChooser?.reason, 'unsupported_chooser_mode');
      const status = await tool('control_status', scope(tab));
      assert.equal(status.pendingFileChooser, undefined);
      await sleep(120);
      assert.equal(fixtureStates.get('chooser-dir').uploads, 0);
      assert.equal(fixtureStates.get('chooser-dir').changes, 0);
      return { rejectedReason: clicked.fileChooser.reason, uploads: 0 };
    });
    await check('Client without uploads permission gets blocked chooser immediately without OS dialog', async () => {
      noUploadsClient = new Client({ name: 'No-uploads regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
      await noUploadsClient.connect(new StdioClientTransport({
        command: process.execPath,
        args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', resolve(profile, 'no-uploads-client-config.json')],
        stderr: 'pipe',
      }));
      const callNoUp = async (name, args) => {
        const r = await noUploadsClient.callTool({ name: 'xenon_' + name, arguments: args });
        assert(!r.isError, name + ': ' + JSON.stringify(r.structuredContent));
        return r.structuredContent;
      };
      const noUpWorker = await callNoUp('worker_create', { name: 'No-uploads worker', workspaceId });
      const tab = { ...noUpWorker, ...await callNoUp('tab_create', { agentSessionId: noUpWorker.agentSessionId, workspaceId, url: origin + '/page?case=chooser-blocked&mode=custom' }) };
      await until(() => fixtureStates.get('chooser-blocked')?.opened);
      const obs = await until(async () => {
        const r = await noUploadsClient.callTool({ name: 'xenon_observe', arguments: { agentSessionId: noUpWorker.agentSessionId, workspaceId, tabId: tab.tabId, maxNodes: 500 } });
        if (r.isError) return false;
        return r.structuredContent.nodes.some(n => n.name === 'Choose approved fixture' && n.ref) ? r.structuredContent : false;
      });
      const btn = obs.nodes.find(n => n.name === 'Choose approved fixture' && n.ref);
      const clicked = await callNoUp('interact', {
        agentSessionId: noUpWorker.agentSessionId, workspaceId, tabId: tab.tabId,
        ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(),
        observationId: obs.observationId, action: 'click', elementRef: btn.ref,
      });
      assert.equal(clicked.fileChooser?.status, 'blocked', JSON.stringify(clicked));
      assert.equal(clicked.fileChooser?.reason, 'uploads_not_permitted');
      const status = await callNoUp('control_status', { agentSessionId: noUpWorker.agentSessionId, workspaceId, tabId: tab.tabId });
      assert.equal(status.pendingFileChooser, undefined);
      await sleep(120);
      assert.equal(fixtureStates.get('chooser-blocked').uploads, 0);
      await noUploadsClient.close();
      noUploadsClient = undefined;
      return { blockedReason: clicked.fileChooser.reason, uploads: 0 };
    });
    await check('Pending chooser auto-cancels cleanly on navigation, unrelated click, denied grant, and handoff', async () => {
      // (a) Navigation cancels pending chooser.
      const tabNav = await open('chooser-nav', 'chooser-multi-action');
      const t1 = await target(tabNav, 'Open Upload Picker', 'BUTTON');
      const c1 = await tool('interact', { ...mutation(tabNav), observationId: t1.observation.observationId, action: 'click', elementRef: t1.node.ref });
      assert.equal(c1.fileChooser?.status, 'pending');
      await tool('navigate', { ...mutation(tabNav), action: 'navigate', url: origin + '/page?case=chooser-nav-after&mode=custom' });
      await until(() => fixtureStates.get('chooser-nav-after')?.opened);
      const staleNav = await raw('upload', { ...mutation(tabNav), chooserId: c1.fileChooser.chooserId, fileId });
      assert.equal(staleNav.isError, true);
      assert.equal(staleNav.structuredContent?.error?.code, 'stale_file_chooser');

      // (b) Unrelated click mutation cancels pending chooser cleanly and still executes.
      const tabMut = await open('chooser-mut', 'chooser-multi-action');
      const t2 = await target(tabMut, 'Open Upload Picker', 'BUTTON');
      const otherBtn = t2.observation.nodes.find(n => n.name === 'Other Action' && n.ref);
      const c2 = await tool('interact', { ...mutation(tabMut), observationId: t2.observation.observationId, action: 'click', elementRef: t2.node.ref });
      assert.equal(c2.fileChooser?.status, 'pending');
      await tool('interact', { ...mutation(tabMut), observationId: t2.observation.observationId, action: 'click', elementRef: otherBtn.ref });
      await until(() => fixtureStates.get('chooser-mut').otherClicks === 1);
      const staleMut = await raw('upload', { ...mutation(tabMut), chooserId: c2.fileChooser.chooserId, fileId });
      assert.equal(staleMut.isError, true);
      assert.equal(staleMut.structuredContent?.error?.code, 'stale_file_chooser');

      // (c) Denied file grant fails with file_denied and auto-cancels the pending chooser.
      const t3 = await target(tabMut, 'Open Upload Picker', 'BUTTON');
      const c3 = await tool('interact', { ...mutation(tabMut), observationId: t3.observation.observationId, action: 'click', elementRef: t3.node.ref });
      assert.equal(c3.fileChooser?.status, 'pending');
      const deniedGrant = await raw('upload', { ...mutation(tabMut), chooserId: c3.fileChooser.chooserId, fileId: 'not_a_granted_file' });
      assert.equal(deniedGrant.isError, true);
      assert.equal(deniedGrant.structuredContent?.error?.code, 'file_denied');
      const afterDenied = await raw('upload', { ...mutation(tabMut), chooserId: c3.fileChooser.chooserId, fileId });
      assert.equal(afterDenied.isError, true);
      assert.equal(afterDenied.structuredContent?.error?.code, 'stale_file_chooser');

      // (d) Ownership handoff cancels pending chooser immediately.
      const peerWorker = await tool('worker_create', { name: 'Chooser handoff peer', workspaceId });
      const t4 = await target(tabMut, 'Open Upload Picker', 'BUTTON');
      const c4 = await tool('interact', { ...mutation(tabMut), observationId: t4.observation.observationId, action: 'click', elementRef: t4.node.ref });
      assert.equal(c4.fileChooser?.status, 'pending');
      const handoff = await tool('control', { ...scope(tabMut), action: 'handoff', expectedGeneration: tabMut.ownershipGeneration, toSessionId: peerWorker.agentSessionId });
      assert.equal(handoff.ownerSessionId, peerWorker.agentSessionId);
      assert.equal(handoff.pendingFileChooser, undefined);
      const peerTry = await raw('upload', {
        agentSessionId: peerWorker.agentSessionId, workspaceId, tabId: tabMut.tabId,
        ownershipGeneration: handoff.ownershipGeneration, operationId: randomUUID(),
        chooserId: c4.fileChooser.chooserId, fileId,
      });
      assert.equal(peerTry.isError, true);
      assert.equal(peerTry.structuredContent?.error?.code, 'stale_file_chooser');
      await tool('worker_retire', { agentSessionId: peerWorker.agentSessionId });
      assert.equal(fixtureStates.get('chooser-nav').uploads, 0);
      assert.equal(fixtureStates.get('chooser-mut').uploads, 0);
      return { navigationCanceled: true, mutationCanceled: true, deniedGrantCanceled: true, handoffCanceled: true };
    });
    await check('xenon_batch stops immediately when step 0 opens a pending file chooser, skips step 1, and lets xenon_upload fulfill it', async () => {
      const tab = await open('chooser-batch', 'chooser-multi-action');
      const { observation, node: trigger } = await target(tab, 'Open Upload Picker', 'BUTTON');
      const other = observation.nodes.find(n => n.name === 'Other Action' && n.ref);
      assert(other);
      const batched = await tool('batch', {
        ...mutation(tab), observationId: observation.observationId,
        steps: [
          { action: 'click', elementRef: trigger.ref },
          { action: 'click', elementRef: other.ref },
        ],
      });
      assert.equal(batched.status, 'stopped', JSON.stringify(batched));
      assert.equal(batched.stoppedAt, 0);
      assert.equal(batched.stopReason, 'file_chooser_pending');
      assert.equal(batched.fileChooser?.status, 'pending');
      assert.equal(batched.steps.length, 2);
      assert.equal(batched.steps[1].status, 'skipped');
      assert.equal(batched.steps[1].dispatchStatus, 'not_dispatched');
      await sleep(120);
      assert.equal(fixtureStates.get('chooser-batch').otherClicks ?? 0, 0, 'Step 1 must not execute while chooser is pending');

      const fulfilled = await raw('upload', { ...mutation(tab), chooserId: batched.fileChooser.chooserId, fileId });
      assert(!fulfilled.isError, JSON.stringify(fulfilled.structuredContent));
      selectionFacts(fulfilled, 'not_attempted', 'selected');
      await until(() => fixtureStates.get('chooser-batch').uploaded && fixtureStates.get('chooser-batch').changes === 1);
      assert.deepEqual(fixtureStates.get('chooser-batch').selectedFiles, ['approved-report.txt']);
      return { stoppedAt: 0, step1Skipped: true, uploadVerified: true };
    });
  }
  if (benchmarkSource) await check(baseline ? 'Exact external health flow reproduces only upload_transfer failure' : 'Exact external health flow passes with final submission untouched', fullHealthFlow);
  if (benchmarkMatrix) {
    await check('External comparison fixture passes the complete MCP workflow', async () => fullHealthFlow(await matrixCandidate('comparison', 0)));
    await check('External calibration fixture passes the complete MCP workflow', async () => fullHealthFlow(await matrixCandidate('calibration', 0)));
    await check('Four external concurrency fixtures preserve distinct data through background MCP input', async () => {
      const candidates=[];
      for(let index=0;index<4;++index){const owner=await tool('worker_create',{name:'Concurrent fixture '+index,workspaceId});candidates.push(await matrixCandidate('concurrency',index,owner));}
      const flows=await Promise.all(candidates.map(fullHealthFlow));
      assert.equal(new Set(flows.map(flow=>flow.grade.expected_sha256)).size,4);
      return { workers:4, flows, modelPerformanceComparison:false };
    });
  }
  }
} catch (error) {
  if (!results.some(result => !result.passed)) results.push({ name: 'Upload entry harness', passed: false, error: error.message });
  console.log('FAIL upload entry harness: ' + error.message);
} finally {
  if (noUploadsClient) await noUploadsClient.close().catch(() => {});
  if (client) await client.close().catch(() => {});
  if (browser?.pid && browser.exitCode === null && browser.signalCode === null) {
    let terminationError; try { await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }); } catch (error) { terminationError = error; }
    try { await until(() => browser.exitCode !== null || browser.signalCode !== null, 10000, 'Owned process exit'); await until(async () => !(await alivePipe()), 15000, 'Private pipe shutdown'); }
    catch (error) { results.push({ name: 'Owned process cleanup', passed: false, error: [terminationError?.message, error.message].filter(Boolean).join('\n') }); }
  }
  for (const server of servers) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
  if (lastObservation) await writeFile(resolve(profile, 'last-observation.json'), JSON.stringify(lastObservation, null, 2));
  await writeFile(resolve(profile, 'xenon-mcp-calls.json'), JSON.stringify({ calls }, null, 2));
  try {
    applicationDllSha256AtEnd = await hashDll(); assert.equal(applicationDllSha256AtEnd, applicationDllSha256, 'Browser DLL changed during test');
    if (benchmarkSource && benchmarkMetadata) { assert.equal(sha256(await readFile(benchmarkSource)), benchmarkMetadata.sourceSha256, 'External source changed during reproduction'); benchmarkMetadata.sourceUnchangedAfterRun = true; }
  } catch (error) { results.push({ name: 'Source and binary stability', passed: false, error: error.message }); }
  const report = { run, capturedAt: new Date().toISOString(), binary, profile,
    applicationDllSha256, applicationDllSha256AtEnd, mode: baseline ? 'baseline_failure_reproduction' : 'acceptance', benchmark: benchmarkMetadata ?? null,
    passed: results.length === (oracleObservation ? 7 : 7 + (baseline ? 0 : 20) + (benchmarkSource ? 1 : 0) + (benchmarkMatrix ? 3 : 0)) && results.every(result => result.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true }); const reportPath = resolve(root, 'out', `${run}${baseline ? '-baseline' : ''}.json`);
  await writeFile(reportPath, JSON.stringify(report, null, 2)); console.log('Report: ' + reportPath); process.exitCode = report.passed ? 0 : 1;
}
