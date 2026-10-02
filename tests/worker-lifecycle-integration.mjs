// Real production CEF and modern/legacy MCP clients. Only this generated
// profile, synthetic credentials, loopback fixture and spawned PID are used.
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

const root = resolve(import.meta.dirname, '..');
const run = `worker-lifecycle-${Date.now()}-${randomBytes(4).toString('hex')}`;
const pipeName = `xenon-test-${run}`;
const pipe = '\\\\.\\pipe\\' + pipeName;
const profile = resolve(root, '.cache', run);
const workspaceId = `lifecycle_workspace_${randomBytes(8).toString('hex')}`;
const binary = resolve(root, 'build/app/Release/Xenon.exe');
const hashDll = async () => createHash('sha256').update(await readFile(resolve(root, 'build/app/Release/Xenon.dll'))).digest('hex');
const results = [], clients = [], sockets = new Set();
const runFile = promisify(execFile);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
let server, browser, origin, telemetry, applicationDllSha256, applicationDllSha256AtEnd, lastObservation;
let ownerClient, otherClient, owner, liveTab, originalDocumentId;

const alivePipe = () => new Promise(resolve => {
  const socket = net.connect(pipe);
  socket.once('connect', () => { socket.destroy(); resolve(true); });
  socket.once('error', () => { socket.destroy(); resolve(false); });
});
async function until(action, timeout = 15000, description = 'Fixture state') {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) { const value = await action(); if (value) return value; await sleep(10); }
  throw new Error(`${description} timed out`);
}
async function check(name, action) {
  const started = Date.now();
  try { const details = await action(); results.push({ name, passed: true, elapsedMs: Date.now() - started, details }); console.log(`PASS ${name}`); }
  catch (error) { results.push({ name, passed: false, elapsedMs: Date.now() - started, error: error.message }); console.log(`FAIL ${name}: ${error.message}`); throw error; }
}
const raw = (client, name, args = {}) => client.callTool({ name: `xenon_${name}`, arguments: args });
async function tool(client, name, args = {}) {
  const response = await raw(client, name, args);
  assert(!response.isError, `${name}: ${JSON.stringify(response.structuredContent ?? response.content)}`);
  return response.structuredContent;
}
const scope = worker => ({ agentSessionId: worker.agentSessionId, workspaceId, tabId: liveTab.tabId });
const mutation = worker => ({ ...scope(worker), ownershipGeneration: liveTab.ownershipGeneration, operationId: randomUUID() });
async function observe(worker = owner, client = ownerClient) {
  const deadline = Date.now() + 5000;
  while (true) {
    const response = await raw(client, 'observe', { ...scope(worker), maxNodes: 1000 });
    lastObservation = response;
    if (response.isError && response.structuredContent?.error?.code === 'privacy_guard_initializing' && Date.now() < deadline) { await sleep(100); continue; }
    assert(!response.isError, `observe: ${JSON.stringify(response.structuredContent ?? response.content)}`);
    return response.structuredContent;
  }
}
const createWorker = (client, name) => tool(client, 'worker_create', { name, workspaceId });
async function acquire(worker, client = ownerClient) {
  const state = await until(async () => {
    const current = await tool(client, 'control_status', scope(worker));
    return current.ownerSessionId === '' && !current.inputBusy && !current.handoffPending ? current : false;
  }, 15000, 'Retired worker releasing its finite input reservation');
  const acquired = await tool(client, 'control', { ...scope(worker), action: 'acquire', expectedGeneration: state.ownershipGeneration });
  assert.equal(acquired.ownerSessionId, worker.agentSessionId); Object.assign(liveTab, acquired);
  return observe(worker, client);
}
async function retire(client, worker) {
  const retired = await tool(client, 'worker_retire', { agentSessionId: worker.agentSessionId });
  assert.equal(retired.agentSessionId, worker.agentSessionId);
  assert(['retired', 'retiring'].includes(retired.status)); return retired;
}
async function readTelemetry(afterSequence = -1) {
  return until(() => telemetry && telemetry.sequence > afterSequence ? structuredClone(telemetry) : false, 5000, 'Fresh website telemetry');
}
function assertPreserved(before, after) {
  for (const key of ['timeOrigin', 'draft', 'active', 'selectionStart', 'selectionEnd', 'scrollX', 'scrollY', 'clicks'])
    assert.deepEqual(after[key], before[key], `Live page state changed: ${key}`);
  assert.deepEqual(after.events, before.events, 'Worker ownership changes emitted website events');
}
async function connect(index, era) {
  const client = new Client({ name: `Xenon lifecycle ${index} ${era}`, version: '1' }, {
    versionNegotiation: { mode: era === 'legacy' ? 'legacy' : { pin: '2026-07-28' } },
  });
  await client.connect(new StdioClientTransport({ command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', resolve(profile, `client-${index}.json`)], stderr: 'pipe' }));
  clients.push(client); return client;
}
async function fixture() {
  const html = `<!doctype html><meta charset="utf-8"><title>Worker lifecycle fixture</title><style>
    body{font:16px system-ui;margin:20px;color:#142237;background:white}
    header{position:sticky;top:0;z-index:2;background:white;padding:8px;border:1px solid #bbb}
    h1{font-size:20px;margin:0}button,input{font:inherit;padding:9px;margin:10px}
    #gesture{display:flex;gap:180px;margin:15px 0}#gesture>div{padding:15px;background:#d7e8fb;border:1px solid #777}
    #spacer{height:2200px}
  </style><header><h1>Worker lifecycle ready</h1><span id="timer">Timer: 0</span> <span id="stream">Stream: 0</span></header>
  <label>Draft <input id="draft" aria-label="Draft" value="Initial draft"></label>
  <button id="increment">Increment lifecycle count</button><output id="count">Clicks: 0</output>
  <div id="gesture"><div id="start" role="button">Lifecycle drag handle</div><div id="end" role="button">Lifecycle drag destination</div></div>
  <p id="gesture-result">Gesture idle</p><div id="spacer"></div><p>Lifecycle fixture bottom</p>
  <script>
    const events=[];let sequence=0,ticks=0,stream=0,clicks=0,mouseHeld=false,gesture='idle';
    const draft=document.querySelector('#draft');
    function record(event){events.push(event.type);if(events.length>300)events.shift()}
    ['focus','blur','visibilitychange','storage','beforeunload','pageshow','pagehide'].forEach(type=>window.addEventListener(type,record));
    ['input','change','pointerdown','pointerup','pointercancel','keydown','keyup','compositionstart','compositionend','focusin','focusout','scroll'].forEach(type=>document.addEventListener(type,record,true));
    function publish(){const state={sequence:++sequence,timeOrigin:performance.timeOrigin,draft:draft.value,active:document.activeElement?.id,selectionStart:draft.selectionStart,selectionEnd:draft.selectionEnd,scrollX,scrollY,clicks,ticks,stream,mouseHeld,gesture,events:[...events]};fetch('/telemetry',{method:'POST',body:JSON.stringify(state)}).catch(()=>{})}
    document.querySelector('#increment').onclick=()=>{document.querySelector('#count').textContent='Clicks: '+(++clicks);publish()};
    document.addEventListener('pointerdown',()=>{mouseHeld=true},true);
    document.addEventListener('pointerup',()=>{mouseHeld=false;gesture=gesture==='down'?'released':gesture;document.querySelector('#gesture-result').textContent='Gesture released';publish()},true);
    document.addEventListener('pointercancel',()=>{mouseHeld=false;gesture='cancelled';publish()},true);
    document.querySelector('#start').addEventListener('pointerdown',()=>{gesture='down';publish()});
    const socket=new WebSocket('ws://'+location.host+'/stream');socket.onmessage=event=>{stream=Number(event.data);document.querySelector('#stream').textContent='Stream: '+stream};
    setInterval(()=>{document.querySelector('#timer').textContent='Timer: '+(++ticks);publish()},100);
  </script>`;
  server = http.createServer((request, response) => {
    if (request.url === '/telemetry' && request.method === 'POST') {
      let body = ''; request.on('data', chunk => { body += chunk; if (body.length > 32768) request.destroy(); });
      request.on('end', () => { try { const next = JSON.parse(body); if (!telemetry || next.sequence > telemetry.sequence) telemetry = next; response.end('ok'); } catch { response.writeHead(400).end(); } }); return;
    }
    response.setHeader('Content-Type', 'text/html; charset=utf-8'); response.setHeader('Cache-Control', 'no-store'); response.end(html);
  });
  server.on('upgrade', (request, socket) => {
    const key = request.headers['sec-websocket-key']; if (typeof key !== 'string') { socket.destroy(); return; }
    const accept = createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    sockets.add(socket); let count = 0;
    const timer = setInterval(() => { const payload = Buffer.from(String(++count)); socket.write(Buffer.concat([Buffer.from([0x81, payload.length]), payload])); }, 100);
    socket.on('error', () => {}); socket.on('close', () => { clearInterval(timer); sockets.delete(socket); });
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  origin = `http://127.0.0.1:${server.address().port}`;
}

if (process.platform !== 'win32') throw new Error('Worker lifecycle regression requires Windows.');
if (await alivePipe()) throw new Error('The randomly allocated worker lifecycle test pipe is already in use.');
try {
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_WORKER_LIFECYCLE_FIXTURE\n');
  const identities = [0, 1].map(index => ({ clientId: `lifecycle_${index}_${randomBytes(8).toString('hex')}`, token: randomBytes(32).toString('hex') }));
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: identities.map((identity, index) => ({ id: identity.clientId, name: `Synthetic lifecycle ${index}`, tokenHash: createHash('sha256').update(identity.token).digest('hex') })),
    workspaces: [{ id: workspaceId, clients: identities.map(identity => identity.clientId) }], accountGrants: [], operations: [] }));
  for (let index = 0; index < identities.length; index++) await writePrivateConfig(resolve(profile, `client-${index}.json`), { ...identities[index], pipe });
  await fixture(); applicationDllSha256 = await hashDll();
  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`, '--max-concurrent-workers=4'], { windowsHide: true, stdio: 'ignore' });
  let launchError; browser.once('error', error => { launchError = error; });
  await until(async () => {
    if (launchError) throw launchError;
    if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Browser exited before worker lifecycle pipe readiness');
    return alivePipe();
  }, 45000, 'Private browser pipe');
  ownerClient = await connect(0, 'modern'); otherClient = await connect(1, 'legacy');
  owner = await createWorker(ownerClient, 'Initial lifecycle owner');
  liveTab = await tool(ownerClient, 'tab_create', { agentSessionId: owner.agentSessionId, workspaceId, url: `${origin}/` });
  await until(async () => (await observe()).nodes.some(node => node.name === 'Worker lifecycle ready'));

  await check('Modern and legacy clients cannot retire each other’s workers', async () => {
    const denied = await raw(otherClient, 'worker_retire', { agentSessionId: owner.agentSessionId });
    assert(denied.isError, 'Another paired client retired the owner');
    const state = await tool(ownerClient, 'control_status', scope(owner));
    assert.equal(state.ownerSessionId, owner.agentSessionId);
    return { modernOwner: true, legacyCaller: true, denied: true };
  });
  await check('Twenty retired workers preserve one live drafted, selected and scrolled session', async () => {
    let evidence = await observe(); originalDocumentId = evidence.documentId;
    const draft = evidence.nodes.find(node => node.role === 'textbox' && node.name === 'Draft' && node.ref); assert(draft);
    const text = 'Unsubmitted draft across twenty worker retirements';
    await tool(ownerClient, 'interact', { ...mutation(owner), observationId: evidence.observationId, action: 'fill', elementRef: draft.ref, text });
    evidence = await observe();
    await tool(ownerClient, 'interact', { ...mutation(owner), observationId: evidence.observationId, action: 'key', key: 'Shift+ArrowLeft' });
    evidence = await observe();
    await tool(ownerClient, 'interact', { ...mutation(owner), observationId: evidence.observationId, action: 'scroll', direction: 'down', amount: 260 });
    await until(() => telemetry?.draft === text && telemetry.selectionStart === text.length - 1 && telemetry.selectionEnd === text.length && telemetry.scrollY > 0 && telemetry.stream > 0);
    await sleep(250); const initial = await readTelemetry();
    const sessions = new Set([owner.agentSessionId]);
    for (let cycle = 0; cycle < 20; cycle++) {
      const before = await readTelemetry();
      const old = owner, oldScope = scope(old);
      assert.equal((await retire(ownerClient, old)).status, 'retired');
      assert((await raw(ownerClient, 'worker_resume', { agentSessionId: old.agentSessionId })).isError, 'Retired worker resumed');
      assert((await raw(ownerClient, 'interact', { ...oldScope, ownershipGeneration: liveTab.ownershipGeneration, operationId: randomUUID(), observationId: evidence.observationId, action: 'key', key: 'ArrowLeft' })).isError, 'Retired worker dispatched input');
      owner = await createWorker(ownerClient, `Replacement ${cycle + 1}`);
      assert(!sessions.has(owner.agentSessionId)); sessions.add(owner.agentSessionId);
      evidence = await acquire(owner);
      assert.equal(evidence.documentId, originalDocumentId); assert.equal(evidence.workspaceId, workspaceId);
      const tabs = await tool(ownerClient, 'tabs', { agentSessionId: owner.agentSessionId, workspaceId });
      assert.equal(tabs.tabs.length, 1); assert.equal(tabs.tabs[0].tabId, liveTab.tabId);
      const after = await readTelemetry(before.sequence); assertPreserved(before, after);
      const workers = await tool(ownerClient, 'workers');
      assert.equal(workers.workers.filter(worker => worker.connected).length, 1);
      assert.equal((await tool(ownerClient, 'workspaces')).workspaces.length, 1, 'Worker replacement created extra persistent profiles');
    }
    const final = await readTelemetry(initial.sequence); assertPreserved(initial, final);
    assert(final.ticks > initial.ticks && final.stream > initial.stream, 'Live timer or WebSocket stopped during replacement');
    return { retirements: 20, uniqueWorkers: sessions.size, liveTabs: 1, grantedWorkspaces: 1, websiteEventsAdded: 0, documentPreserved: true, timerAndWebSocketContinue: true };
  });
  await check('Retirement cancels queued input and releases a started finite gesture honestly', async () => {
    let evidence = await observe();
    await tool(ownerClient, 'interact', { ...mutation(owner), observationId: evidence.observationId, action: 'scroll', direction: 'up', amount: 3000 });
    await until(() => telemetry?.scrollY === 0);
    evidence = await observe();
    const from = evidence.nodes.find(node => node.role === 'button' && node.name === 'Lifecycle drag handle' && node.ref);
    const to = evidence.nodes.find(node => node.role === 'button' && node.name === 'Lifecycle drag destination' && node.ref);
    const click = evidence.nodes.find(node => node.role === 'button' && node.name === 'Increment lifecycle count' && node.ref); assert(from && to && click);
    const old = owner, startedId = randomUUID(), queuedId = randomUUID();
    const started = raw(ownerClient, 'interact', { ...mutation(old), operationId: startedId, observationId: evidence.observationId, action: 'drag', fromRef: from.ref, toRef: to.ref });
    await until(() => telemetry?.gesture === 'down' && telemetry.mouseHeld, 5000, 'Real website pointer-down');
    const queued = raw(ownerClient, 'interact', { ...mutation(old), operationId: queuedId, observationId: evidence.observationId, action: 'click', elementRef: click.ref });
    await until(async () => { const response = await raw(ownerClient, 'operation', { operationId: queuedId }); return !response.isError && response.structuredContent.state === 'queued'; }, 1500, 'Broker queue reservation');
    const retired = await retire(ownerClient, old);
    const queuedResult = await queued; assert(queuedResult.isError); assert.equal(queuedResult.structuredContent.dispatchStatus, 'not_dispatched');
    const startedResult = await started;
    assert.equal(startedResult.structuredContent.dispatchStatus, 'dispatched', 'Website observed pointer-down, so the outcome must report dispatch');
    const operation = await tool(ownerClient, 'operation', { operationId: startedId });
    assert(['completed', 'failed', 'outcome_unknown'].includes(operation.state), 'Started input needs a terminal honest outcome');
    await until(() => telemetry && !telemetry.mouseHeld && ['released', 'cancelled'].includes(telemetry.gesture), 5000, 'Balancing pointer release');
    assert.equal(telemetry.clicks, 0, 'Queued input replayed after retirement');
    owner = await createWorker(ownerClient, 'After finite retirement');
    evidence = await acquire(owner); assert.equal(evidence.documentId, originalDocumentId);
    assert((await raw(ownerClient, 'worker_resume', { agentSessionId: old.agentSessionId })).isError);
    return { retirementStatus: retired.status, startedOperation: operation.state, queuedDispatchStatus: queuedResult.structuredContent.dispatchStatus, heldMouseReleased: true, queuedClicks: telemetry.clicks };
  });
  await check('Configured global capacity spans clients and retirement frees a slot', async () => {
    const others = [];
    for (let index = 0; index < 3; index++) others.push(await createWorker(otherClient, `Legacy capacity ${index}`));
    const denied = await raw(ownerClient, 'worker_create', { name: 'Overflow', workspaceId });
    assert(denied.isError); assert.equal(denied.structuredContent.error.code, 'CAPACITY_EXCEEDED');
    await tool(ownerClient, 'worker_resume', { agentSessionId: owner.agentSessionId }); // Same attachment consumes no new slot.
    await retire(otherClient, others.pop());
    const replacement = await createWorker(ownerClient, 'Capacity replacement');
    const before = await readTelemetry();
    await otherClient.close();
    otherClient = await connect(1, 'legacy');
    const disconnected = await until(async () => { const listed = await tool(otherClient, 'workers'); return others.every(worker => listed.workers.some(item => item.agentSessionId === worker.agentSessionId && !item.connected && item.state === 'disconnected')) ? listed : false; });
    assert(disconnected.workers.length >= 2, 'Disconnected handles should remain resumable');
    const fillA = await createWorker(ownerClient, 'Freed disconnected slot A');
    const fillB = await createWorker(ownerClient, 'Freed disconnected slot B');
    const resumeDenied = await raw(otherClient, 'worker_resume', { agentSessionId: others[0].agentSessionId });
    assert(resumeDenied.isError); assert.equal(resumeDenied.structuredContent.error.code, 'CAPACITY_EXCEEDED');
    await retire(ownerClient, fillB);
    const resumed = await tool(otherClient, 'worker_resume', { agentSessionId: others[0].agentSessionId });
    assert.equal(resumed.agentSessionId, others[0].agentSessionId);
    await tool(otherClient, 'worker_resume', { agentSessionId: others[0].agentSessionId });
    const after = await readTelemetry(before.sequence); assertPreserved(before, after);
    assert.equal(after.clicks, 0, 'Reconnect replayed a browser mutation');
    assert.equal((await observe()).documentId, originalDocumentId);
    const tabs = await tool(ownerClient, 'tabs', { agentSessionId: owner.agentSessionId, workspaceId });
    assert.equal(tabs.tabs.length, 1); assert.equal(tabs.tabs[0].tabId, liveTab.tabId);
    return { configuredConnectedCap: 4, sharedAcrossClients: true, retiredSlotReused: true, disconnectedHandlesRetained: others.length,
      disconnectedSlotsReused: 2, resumeRefusedAtCapacity: true, resumeSucceededAfterRetirement: true, noAutomaticReplay: true,
      additionalConnectedWorkers: [replacement.agentSessionId, fillA.agentSessionId].length };
  });
} catch (error) {
  if (!results.some(result => !result.passed)) results.push({ name: 'Worker lifecycle harness', passed: false, error: error.message });
  console.log(`FAIL lifecycle harness: ${error.message}`);
} finally {
  try {
    if (lastObservation) await writeFile(resolve(profile, 'last-observation.json'), JSON.stringify(lastObservation, null, 2));
    if (telemetry) await writeFile(resolve(profile, 'telemetry.json'), JSON.stringify(telemetry, null, 2));
  } catch (error) { results.push({ name: 'Synthetic diagnostics', passed: false, error: error.message }); }
  for (const client of clients) await client.close().catch(() => {});
  if (browser?.pid && browser.exitCode === null && browser.signalCode === null) try {
    let terminationError;
    try { await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }); }
    catch (error) { terminationError = error; }
    try {
      // taskkill can report a child that exited during tree termination. That
      // result is benign only after both owned parent and private pipe are gone.
      await until(() => browser.exitCode !== null || browser.signalCode !== null, 10000, 'Owned browser process exit');
      await until(async () => !(await alivePipe()), 15000, 'Private pipe cleanup');
    } catch (error) {
      throw new Error([terminationError?.message, error.message].filter(Boolean).join('\n'));
    }
  } catch (error) { results.push({ name: 'Owned process cleanup', passed: false, error: error.message }); }
  for (const socket of sockets) socket.destroy();
  if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
  try { applicationDllSha256AtEnd = await hashDll(); assert.equal(applicationDllSha256AtEnd, applicationDllSha256, 'DLL changed during worker lifecycle test'); }
  catch (error) { results.push({ name: 'Tested binary stability', passed: false, error: error.message }); }
  const report = { run, capturedAt: new Date().toISOString(), binary: 'build/app/Release/Xenon.exe', profile, applicationDllSha256,
    applicationDllSha256AtEnd, configuredConnectedCap: 4, passed: results.length === 4 && results.every(result => result.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true });
  const reportPath = resolve(root, 'out/worker-lifecycle-integration-results.json');
  await writeFile(reportPath, JSON.stringify(report, null, 2)); console.log(`Report: ${reportPath}`); process.exitCode = report.passed ? 0 : 1;
}
