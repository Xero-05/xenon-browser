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
const run = 'upload-entry-' + Date.now() + '-' + randomBytes(4).toString('hex');
const profile = resolve(root, '.cache', 'auth-integration-' + run), uploadsRoot = profile + '-uploads';
const pipeName = 'xenon-test-' + run, slash = String.fromCharCode(92), pipe = slash + slash + '.' + slash + 'pipe' + slash + pipeName;
const binary = resolve(root, 'build/app/Release/Xenon.exe'), dll = resolve(root, 'build/app/Release/Xenon.dll');
const baseline = process.argv.includes('--baseline'), benchmarkIndex = process.argv.indexOf('--benchmark');
if (benchmarkIndex >= 0 && !process.argv[benchmarkIndex + 1]) throw new Error('--benchmark requires the source path');
const benchmarkSource = benchmarkIndex >= 0 ? resolve(process.argv[benchmarkIndex + 1]) : undefined;
const benchmarkMatrix = process.argv.includes('--benchmark-matrix');
if (benchmarkMatrix && (!benchmarkSource || baseline)) throw new Error('--benchmark-matrix requires --benchmark and acceptance mode');
const workspaceId = 'auth_fixture_shared';
const results = [], calls = [], servers = [], fixtureStates = new Map();
const runFile = promisify(execFile), sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const hashDll = async () => sha256(await readFile(dll));
let browser, client, worker, otherWorker, fileId, folderId, origin, crossOrigin, applicationDllSha256, applicationDllSha256AtEnd;
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
    if (response.isError && response.structuredContent?.error?.code === 'privacy_guard_initializing' && Date.now() < deadline) { await sleep(100); continue; }
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
  await until(async () => (await observe(tab)).nodes.some(node => node.name === 'Upload entry fixture ready'));
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
          try { const value = JSON.parse(body); if (value.type === 'opened') state.opened = true; if (value.type === 'click') { ++state.clicks; state.trusted.push(value.trusted); } if (value.type === 'change') { ++state.changes; state.selectedFiles = value.files; } if (value.type === 'label') state.labelChanged = true; response.end('ok'); }
          catch { response.writeHead(400).end(); }
        } else response.writeHead(404).end();
      }); return;
    }
    if (url.pathname !== '/page') { response.writeHead(404).end(); return; }
    const mode = url.searchParams.get('mode'), frame = mode === 'frame';
    response.setHeader('Content-Type', 'text/html; charset=utf-8');
    if (frame) { response.end('<!doctype html><style>body{background:white;color:#152235;font:18px system-ui;margin:20px}iframe{width:700px;height:270px;border:1px solid #777}</style><h1>Upload entry fixture ready</h1><iframe src="' + crossOrigin + '/page?case=' + caseId + '&mode=custom"></iframe>'); return; }
    if (mode === 'delegate') {
      response.end('<!doctype html><style>body{background:white;color:#152235;font:18px system-ui;margin:20px}button{font:inherit;background:white;color:#152235;padding:10px}iframe{display:block;width:650px;height:180px}</style><h1>Upload entry fixture ready</h1><button id="choose">Choose approved fixture</button><iframe id="child" src="/page?case=' + caseId + '-child&mode=child-hidden"></iframe><script>' +
        'const caseId=' + JSON.stringify(caseId) + ';function event(v){fetch("/event?case="+caseId,{method:"POST",body:JSON.stringify(v)})}event({type:"opened"});document.querySelector("#choose").onclick=e=>{event({type:"click",trusted:e.isTrusted});document.querySelector("#child").contentDocument.querySelector("#file").click()};</script>'); return;
    }
    const direct = ['direct', 'direct-disabled', 'direct-directory'].includes(mode), label = mode === 'none' ? 'No chooser button' : 'Choose approved fixture';
    const inputFlags = (mode === 'direct-disabled' ? 'disabled ' : '') + (mode.includes('directory') ? 'webkitdirectory directory ' : '') + (mode === 'multiple' ? 'multiple ' : '');
    response.end('<!doctype html><meta charset="utf-8"><title>Upload entry fixture ' + caseId + '</title><style>body{background:white;color:#152235;font:18px system-ui;margin:20px}input,button{font:inherit;margin:12px;padding:10px;color:#152235;background:white}[hidden]{display:none!important}</style>' +
      '<h1>Upload entry fixture ready</h1><label>' + (direct ? 'Direct upload ' : '') + '<input id="file" type="file" ' + inputFlags + (direct ? '' : 'hidden') + '></label>' + (direct || mode === 'child-hidden' ? '' : '<button id="choose" ' + (mode === 'button-disabled' ? 'disabled' : '') + '>' + label + '</button>') + '<p id="status">No file transferred</p>' +
      '<script>const caseId=' + JSON.stringify(caseId) + ',mode=' + JSON.stringify(mode) + ';const file=document.querySelector("#file"),button=document.querySelector("#choose");' +
      'function event(value){return fetch("/event?case="+caseId,{method:"POST",body:JSON.stringify(value)})}event({type:"opened"});' +
      'if(button)button.onclick=e=>{event({type:"click",trusted:e.isTrusted});if(mode!=="none")file.click()};' +
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
  const seeded = JSON.parse((await runFile(resolve(root, 'build/Release/vault_fixture_seed.exe'), [profile], { windowsHide: true })).stdout.trim()); folderId = seeded.folderId;
  const identity = { clientId: 'upload_' + randomBytes(8).toString('hex'), token: randomBytes(32).toString('hex'), pipe };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic upload client', tokenHash: sha256(identity.token) }],
    workspaces: [{ id: workspaceId, clients: [identity.clientId] }], accountGrants: [], operations: [] }));
  await writePrivateConfig(resolve(profile, 'client-config.json'), identity);
  await prepareBenchmark(); await startFixtures(); applicationDllSha256 = await hashDll(); await launch();
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
  await check('A visible button without a file chooser cannot receive a file', async () => {
    const tab = await open('no-chooser', 'none'), response = await upload(tab, 'No chooser button'); assert(response.isError);
    assert.equal(fixtureStates.get('no-chooser').uploads, 0);
    if (!baseline) { selectionFacts(response, 'dispatched', 'not_selected'); await until(() => fixtureStates.get('no-chooser').clicks === 1); assert.deepEqual(fixtureStates.get('no-chooser').trusted, [true]); }
    return { denied: true, code: response.structuredContent?.error?.code, activatedOnce: !baseline, uploads: 0 };
  });
  await check('Invalid and cross-workspace grants are denied before entry activation', async () => {
    for (const [caseId, owner, grant] of [['invalid-grant', worker, 'not_a_granted_file'], ['cross-workspace', otherWorker, fileId]]) {
      const tab = await open(caseId, 'custom', owner), response = await upload(tab, 'Choose approved fixture', grant);
      assert(response.isError); assert.equal(response.structuredContent?.error?.code, 'file_denied');
      selectionFacts(response, 'not_attempted', 'not_selected');
      await sleep(150); // Let any incorrectly dispatched website events arrive.
      assert.equal(fixtureStates.get(caseId).clicks, 0); assert.equal(fixtureStates.get(caseId).uploads, 0);
    }
    return { denials: 2, activations: 0, uploads: 0 };
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
} catch (error) {
  if (!results.some(result => !result.passed)) results.push({ name: 'Upload entry harness', passed: false, error: error.message });
  console.log('FAIL upload entry harness: ' + error.message);
} finally {
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
  const report = { run, capturedAt: new Date().toISOString(), binary: 'build/app/Release/Xenon.exe', profile,
    applicationDllSha256, applicationDllSha256AtEnd, mode: baseline ? 'baseline_failure_reproduction' : 'acceptance', benchmark: benchmarkMetadata ?? null,
    passed: results.length === 6 + (baseline ? 0 : 3) + (benchmarkSource ? 1 : 0) + (benchmarkMatrix ? 3 : 0) && results.every(result => result.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true }); const reportPath = resolve(root, baseline ? 'out/upload-entry-baseline-results.json' : 'out/upload-entry-integration-results.json');
  await writeFile(reportPath, JSON.stringify(report, null, 2)); console.log('Report: ' + reportPath); process.exitCode = report.passed ? 0 : 1;
}
