// Production CEF + MCP with a generated profile and two process launches.
// At READY FOR NATIVE EXIT, use a fixture Chrome window's menu > Exit.
// Do not close individual tabs: this regression keeps the complete session open.
// No existing browser profile, raw CDP, credential or real website is accessed.
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
const run = `startup-integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile = resolve(root, '.cache', run);
const pipeName = `xenon-test-${run}`, pipePath = `\\\\.\\pipe\\${pipeName}`;
const binary = resolve(root, 'build/app/Release/Xenon.exe');
const applicationDll = resolve(root, 'build/app/Release/Xenon.dll');
const configPath = resolve(profile, 'client-config.json');
const workspaceId = 'native-default';
const fixtureTitle = `Xenon startup fixture ${run.slice(-8)}`;
const storageValue = `startup_${randomBytes(12).toString('hex')}`;
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const runFile = promisify(execFile);
const hashDll = async () => createHash('sha256').update(await readFile(applicationDll)).digest('hex');
const results = [], clients = [], launches = [], telemetry = new Map();
const documentLoads = new Map();
const lastObservations = new Map();
const seededStartupPreferences = [];
let actionCount = 0, browser, server, base, applicationDllSha256, applicationDllSha256AtEnd;
let firstExitCode, firstBlankId, firstWorker, firstFixtureTabs, countsAtExit;
let freshWorker, freshFixture;

const alivePipe = () => new Promise(resolve => {
  const socket = net.connect(pipePath);
  socket.once('connect', () => { socket.destroy(); resolve(true); });
  socket.once('error', () => { socket.destroy(); resolve(false); });
});
async function until(fn, timeout = 20_000, label = 'Fixture condition') {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const result = await fn();
    if (result) return result;
    await sleep(120);
  }
  throw new Error(`${label} timed out`);
}
async function check(name, action) {
  const start = Date.now();
  try {
    const details = await action();
    results.push({ name, passed: true, elapsedMs: Date.now() - start, details });
    console.log(`PASS ${name}`);
  } catch (error) {
    results.push({ name, passed: false, elapsedMs: Date.now() - start, error: error.message });
    console.log(`FAIL ${name}: ${error.message}`);
    throw error;
  }
}
async function tool(client, name, args = {}) {
  const reply = await client.callTool({ name: `xenon_${name}`, arguments: args });
  if (reply.isError) throw new Error(`${name}: ${JSON.stringify(reply.structuredContent ?? reply.content)}`);
  return reply.structuredContent;
}
const workerScope = worker => ({ agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId });
const scope = tab => ({ ...workerScope(tab), tabId: tab.tabId });
async function observation(client, tab, predicate = value => !value.loading) {
  let lastError;
  return until(async () => {
    try {
      const value = await tool(client, 'observe', { ...scope(tab), maxNodes: 300 });
      if (tab.page) lastObservations.set(tab.page, { url: value.url, loading: value.loading,
        names: (value.nodes ?? []).map(node => node.name).filter(Boolean).slice(0, 30) });
      lastError = undefined;
      return predicate(value) ? value : false;
    } catch (error) { lastError = error.message; return false; }
  }, 20_000, 'Expected browser observation').catch(error => {
    throw new Error(`${error.message}${lastError ? `; last observation: ${lastError}` : ''}; synthetic fixture state: ${JSON.stringify({
      page: tab.page, telemetry: telemetry.get(tab.page), observation: lastObservations.get(tab.page),
    })}`);
  });
}
async function blankStartup(client, worker) {
  const tabs = await until(async () => {
    const value = (await tool(client, 'tabs', workerScope(worker))).tabs;
    return value.length ? value : false;
  }, 20_000, 'Initial human tab');
  assert.equal(tabs.length, 1, 'Startup must contain exactly one native-default tab');
  assert.equal(tabs[0].ownerSessionId, 'human', 'Startup tab must remain under human control');
  const observed = await observation(client, { ...worker, ...tabs[0] });
  assert.equal(observed.url, 'about:blank', 'Startup must load a blank page');
  assert.equal((await tool(client, 'tabs', workerScope(worker))).tabs.length, 1);
  return tabs[0];
}
async function openFixture(client, worker, page) {
  const url = new URL('/page', base); url.searchParams.set('page', page);
  return { ...worker, ...await tool(client, 'tab_create', { ...workerScope(worker), url: url.href }), page };
}
async function stateEvidence(client, tab, value) {
  return observation(client, tab, observed => {
    const names = observed.nodes?.map(node => node.name) ?? [];
    return names.includes('Startup fixture ready') &&
      names.includes(`Persistent cookie: ${value ?? '[empty]'}`) &&
      names.includes(`Session cookie: ${value ?? '[empty]'}`) &&
      names.includes(`Local storage: ${value ?? '[empty]'}`);
  });
}
async function connect() {
  const transport = new StdioClientTransport({ command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath], stderr: 'pipe' });
  const client = new Client({ name: 'Xenon startup regression', version: '1' },
    { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(transport); clients.push(client); return client;
}
async function launch() {
  assert.equal(await hashDll(), applicationDllSha256, 'Application changed between launches');
  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { windowsHide: true, stdio: 'ignore' });
  let launchError;
  browser.once('error', error => { launchError = error; });
  launches.push({ pid: browser.pid, startedAt: new Date().toISOString() });
  await until(async () => {
    if (launchError) throw launchError;
    if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Browser exited before its private pipe became ready');
    return alivePipe();
  }, 45_000, 'Browser private pipe');
  return connect();
}
async function stopSpawnedBrowser() {
  const target = browser; browser = undefined;
  // Cleanup targets only the process tree this harness launched, never a name.
  if (target?.pid && target.exitCode === null && target.signalCode === null)
    await runFile('taskkill.exe', ['/PID', String(target.pid), '/T', '/F'], { windowsHide: true });
  if (target?.pid) await until(() => target.exitCode !== null || target.signalCode !== null, 10_000, 'Spawned process cleanup');
  await until(async () => !(await alivePipe()), 15_000, 'Spawned pipe cleanup');
}
const counts = () => ({ actionCount, documentLoads: Object.fromEntries(documentLoads) });
async function prepareStartupPreferences(phase, afterNativeExit = false) {
  assert.equal(browser, undefined, 'Fixture startup preferences may only be seeded while its browser is stopped');
  assert.equal(await alivePipe(), false, 'Fixture pipe must be closed before offline preference seeding');
  const directory = resolve(profile, `workspace-${createHash('sha256').update(workspaceId).digest('hex')}`);
  await mkdir(directory, { recursive: true });
  const readJson = async path => {
    try { return JSON.parse(await readFile(path, 'utf8')); }
    catch (error) { if (error.code === 'ENOENT') return {}; throw error; }
  };
  const path = resolve(directory, 'Preferences'), preferences = await readJson(path);
  if (afterNativeExit) {
    // Chromium tracks this setting in Secure Preferences on Windows. Read the
    // effective stored value without rewriting either store or its integrity data.
    const securePreferences = await readJson(resolve(directory, 'Secure Preferences'));
    const tracked = Object.hasOwn(securePreferences.session ?? {}, 'restore_on_startup');
    const restorePreferenceObserved = tracked ? securePreferences.session.restore_on_startup : preferences.session?.restore_on_startup;
    const restorePreferenceSource = tracked ? 'Secure Preferences' : 'Preferences';
    assert.equal(restorePreferenceObserved, 1,
      'Native Exit must persist the session-cookie retention preference itself; the harness must not repair it');
    const localStatePath = resolve(profile, 'Local State'), localState = await readJson(localStatePath);
    localState.was ??= {};
    assert.equal(typeof localState.was, 'object');
    localState.was.restarted = true;
    await writeFile(localStatePath, JSON.stringify(localState));
    seededStartupPreferences.push({ phase, restorePreferenceObserved, restorePreferenceSource, restorePreferenceWritten: false, wasRestarted: true });
  } else {
    preferences.session ??= {};
    assert.equal(typeof preferences.session, 'object');
    preferences.session.restore_on_startup = 1;
    await writeFile(path, JSON.stringify(preferences));
    seededStartupPreferences.push({ phase, restoreOnStartupSeeded: 1, wasRestarted: false });
  }
}
function startFixture() {
  const cookies = req => {
    const parsed = new Map((req.headers.cookie ?? '').split(';').map(part => part.trim().split('=')));
    return { persistent: parsed.get('xenon_startup_persistent') ?? null, session: parsed.get('xenon_startup_session') ?? null };
  };
  server = http.createServer((req, res) => {
    const url = new URL(req.url, base ?? 'http://127.0.0.1');
    res.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/write' && req.method === 'POST') {
      ++actionCount;
      res.setHeader('Set-Cookie', [
        `xenon_startup_persistent=${storageValue}; Max-Age=86400; Path=/; HttpOnly; SameSite=Lax`,
        `xenon_startup_session=${storageValue}; Path=/; HttpOnly; SameSite=Lax`,
      ]);
      res.end('stored'); return;
    }
    if (url.pathname === '/state') {
      res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(cookies(req))); return;
    }
    if (url.pathname === '/telemetry' && req.method === 'POST') {
      let body = '';
      req.on('data', chunk => { body += chunk; if (body.length > 4096) req.destroy(); });
      req.on('end', () => {
        try { const value = JSON.parse(body); telemetry.set(url.searchParams.get('page'), { ...cookies(req), localStorage: value.localStorage }); res.end('ok'); }
        catch { res.writeHead(400).end(); }
      }); return;
    }
    if (url.pathname !== '/page') { res.writeHead(404).end(); return; }
    const page = url.searchParams.get('page') ?? '';
    if (!/^[a-z0-9-]{1,50}$/.test(page)) { res.writeHead(400).end(); return; }
    documentLoads.set(page, (documentLoads.get(page) ?? 0) + 1);
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    res.end(`<!doctype html><meta charset="utf-8"><title>${fixtureTitle} ${page}</title>
<style>body{background:white;color:black;font:18px Arial;margin:24px}button{font:18px Arial;padding:8px}</style>
<h1 id="ready">Loading startup fixture</h1><p id="persistent"></p><p id="session"></p><p id="storage"></p>
<button id="write">Write fixture state once</button>
<script>
const page=${JSON.stringify(page)},savedValue=${JSON.stringify(storageValue)},key='xenon-startup-fixture';
async function refresh(){const state=await(await fetch('/state',{cache:'no-store'})).json(),stored=localStorage.getItem(key);
document.querySelector('#persistent').textContent='Persistent cookie: '+(state.persistent??'[empty]');
document.querySelector('#session').textContent='Session cookie: '+(state.session??'[empty]');
document.querySelector('#storage').textContent='Local storage: '+(stored??'[empty]');
await fetch('/telemetry?page='+encodeURIComponent(page),{method:'POST',body:JSON.stringify({localStorage:stored})});
document.querySelector('#ready').textContent='Startup fixture ready';}
document.querySelector('#write').addEventListener('click',async()=>{await fetch('/write',{method:'POST'});localStorage.setItem(key,savedValue);await refresh();});
refresh().catch(()=>{document.querySelector('#ready').textContent='Startup fixture failed';});
</script>`);
  });
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => { base = `http://127.0.0.1:${server.address().port}`; resolve(); });
  });
}

if (process.platform !== 'win32') throw new Error('Startup integration requires Windows.');
if (await alivePipe()) throw new Error('Generated test pipe is already in use. Start a fresh run.');
try {
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_STARTUP_FIXTURE\n');
  const identity = { clientId: `startup_${randomBytes(8).toString('hex')}`, token: randomBytes(32).toString('hex'), pipe: pipePath };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic startup client', tokenHash: createHash('sha256').update(identity.token).digest('hex') }],
    workspaces: [{ id: workspaceId, clients: [identity.clientId] }], accountGrants: [], operations: [] }));
  await writePrivateConfig(configPath, identity);
  await prepareStartupPreferences('before-first-launch');
  await startFixture();
  applicationDllSha256 = await hashDll();
  const first = await launch();
  firstWorker = await tool(first, 'worker_create', { name: 'Startup native workspace observer', workspaceId });
  await check('First launch opens exactly one human-owned blank tab', async () => {
    const tab = await blankStartup(first, firstWorker); firstBlankId = tab.tabId;
    assert.equal(documentLoads.size, 0);
    return { initialTabCount: 1, owner: 'human', url: 'about:blank', acquired: false };
  });
  await check('Two fixture tabs remain open with persisted state before native Exit', async () => {
    const a = await openFixture(first, firstWorker, 'before-exit-a');
    const empty = await stateEvidence(first, a, null);
    const button = empty.nodes.find(node => node.role === 'button' && node.name === 'Write fixture state once' && node.ref);
    assert(button, 'Fixture write button must be observed');
    await tool(first, 'interact', { ...scope(a), ownershipGeneration: a.ownershipGeneration,
      operationId: randomUUID(), observationId: empty.observationId, action: 'click', elementRef: button.ref });
    await stateEvidence(first, a, storageValue);
    const b = await openFixture(first, firstWorker, 'before-exit-b');
    await stateEvidence(first, b, storageValue);
    firstFixtureTabs = [a.tabId, b.tabId];
    const tabs = (await tool(first, 'tabs', workerScope(firstWorker))).tabs;
    assert.equal(tabs.length, 3, 'Initial human tab and both fixture tabs must remain open');
    assert(tabs.some(tab => tab.tabId === firstBlankId && tab.ownerSessionId === 'human'));
    assert(firstFixtureTabs.every(id => tabs.some(tab => tab.tabId === id && tab.ownerSessionId === firstWorker.agentSessionId)));
    assert.equal(actionCount, 1); assert.equal(documentLoads.get('before-exit-a'), 1); assert.equal(documentLoads.get('before-exit-b'), 1);
    return { openTabs: 3, fixtureTabs: 2, stateWrites: 1, persistentHttpOnlyCookie: true, sessionHttpOnlyCookie: true, localStorage: true };
  });
  await check('Native whole-application Exit closes the first process successfully', async () => {
    const ready = { run, pid: browser.pid, fixtureWindowTitles: [`${fixtureTitle} before-exit-a`, `${fixtureTitle} before-exit-b`],
      controlWindowTitle: 'Xenon Controls', action: 'Use one listed fixture browser window menu > Exit; do not close individual tabs or another browser.', openTabs: 3 };
    await writeFile(resolve(profile, 'ready-for-native-exit.json'), JSON.stringify(ready, null, 2));
    console.log(`READY FOR NATIVE EXIT ${JSON.stringify(ready)}`);
    const target = browser;
    await until(() => target.exitCode !== null || target.signalCode !== null, 300_000, 'Native whole-application Exit');
    firstExitCode = target.exitCode;
    assert.equal(target.signalCode, null, 'First process must not be terminated by a signal');
    assert.equal(firstExitCode, 0, 'Native Exit must complete successfully');
    browser = undefined;
    await until(async () => !(await alivePipe()), 15_000, 'Native pipe shutdown');
    await first.close();
    countsAtExit = counts();
    return { exitCode: firstExitCode, exitMethod: 'native menu Exit', fixtureTabsClosedByHarness: false };
  });
  await prepareStartupPreferences('before-second-launch', true);
  const second = await launch();
  const secondWorker = await tool(second, 'worker_create', { name: 'Startup observer after restart', workspaceId });
  await check('Normal reopen starts blank without loading old tabs or replaying actions', async () => {
    assert.notEqual(secondWorker.agentSessionId, firstWorker.agentSessionId);
    const blank = await blankStartup(second, secondWorker);
    assert.notEqual(blank.tabId, firstBlankId); assert(!firstFixtureTabs.includes(blank.tabId));
    // Observe a bounded startup settling interval; do not create or navigate a tab.
    const deadline = Date.now() + 3000;
    while (Date.now() < deadline) {
      const tabs = (await tool(second, 'tabs', workerScope(secondWorker))).tabs;
      assert.equal(tabs.length, 1, 'No prior tab may appear after startup settles');
      assert.equal(tabs[0].tabId, blank.tabId);
      assert.deepEqual(counts(), countsAtExit, 'No old fixture document or mutation may run during startup');
      await sleep(150);
    }
    const observed = await observation(second, { ...secondWorker, ...blank });
    assert.equal(observed.url, 'about:blank');
    return { initialTabCount: 1, owner: 'human', url: 'about:blank', oldDocumentLoads: 0, actionReplays: 0, settlingIntervalMs: 3000 };
  });
  await check('Explicit fixture reopen preserves cookies and local storage', async () => {
    const reopened = await openFixture(second, secondWorker, 'explicit-reopen');
    await stateEvidence(second, reopened, storageValue);
    assert.deepEqual(telemetry.get(reopened.page), { persistent: storageValue, session: storageValue, localStorage: storageValue });
    assert.equal(actionCount, 1, 'Preserved state must not depend on a replayed write');
    assert.equal(documentLoads.get('before-exit-a'), 1); assert.equal(documentLoads.get('before-exit-b'), 1);
    return { persistentCookieRetained: true, sessionCookieRetained: true, localStorageRetained: true, repeatedWrites: 0 };
  });
  await check('A fresh workspace does not inherit the native workspace session', async () => {
    const fresh = await tool(second, 'worker_create', { name: 'Fresh startup isolation control' });
    assert.notEqual(fresh.workspaceId, workspaceId);
    const isolated = await openFixture(second, fresh, 'fresh-workspace');
    freshWorker = fresh; freshFixture = isolated;
    await stateEvidence(second, isolated, null);
    assert.deepEqual(telemetry.get(isolated.page), { persistent: null, session: null, localStorage: null });
    assert.equal(actionCount, 1);
    return { persistentCookieEmpty: true, sessionCookieEmpty: true, localStorageEmpty: true };
  });
  await check('Closing a workspace only tab preserves its session while the browser stays open', async () => {
    const empty = await stateEvidence(second, freshFixture, null);
    const button = empty.nodes.find(node => node.role === 'button' && node.name === 'Write fixture state once' && node.ref);
    assert(button, 'Fresh workspace write button must be observed');
    await tool(second, 'interact', { ...scope(freshFixture), ownershipGeneration: freshFixture.ownershipGeneration,
      operationId: randomUUID(), observationId: empty.observationId, action: 'click', elementRef: button.ref });
    await stateEvidence(second, freshFixture, storageValue);
    assert.deepEqual(telemetry.get(freshFixture.page), { persistent: storageValue, session: storageValue, localStorage: storageValue });
    assert.equal(actionCount, 2, 'Exactly one write in each workspace establishes the fixture');
    const before = (await tool(second, 'tabs', workerScope(freshWorker))).tabs;
    assert.equal(before.length, 1, 'The fresh workspace must have exactly one tab before closure');
    assert.equal(before[0].tabId, freshFixture.tabId);
    assert.equal(before[0].ownerSessionId, freshWorker.agentSessionId, 'Only the synthetic worker may close its own tab');
    await tool(second, 'tab_close', { ...scope(freshFixture), ownershipGeneration: freshFixture.ownershipGeneration, operationId: randomUUID() });
    await until(async () => (await tool(second, 'tabs', workerScope(freshWorker))).tabs.length === 0,
      15_000, 'Fresh workspace last tab closure');
    assert((await tool(second, 'tabs', workerScope(secondWorker))).tabs.length >= 2,
      'The native workspace must remain open while the fresh workspace has no windows');
    assert.equal(browser.exitCode, null, 'Closing this workspace must not terminate the application');
    const reopened = await openFixture(second, freshWorker, 'fresh-after-last-close');
    assert.notEqual(reopened.tabId, freshFixture.tabId);
    await stateEvidence(second, reopened, storageValue);
    const expectedNames = [`Persistent cookie: ${storageValue}`, `Session cookie: ${storageValue}`, `Local storage: ${storageValue}`];
    const deadline = Date.now() + 1000;
    do {
      const observed = await tool(second, 'observe', { ...scope(reopened), maxNodes: 300 });
      const names = (observed.nodes ?? []).map(node => node.name);
      lastObservations.set(reopened.page, { url: observed.url, loading: observed.loading, names: names.filter(Boolean).slice(0, 30) });
      assert(expectedNames.every(name => names.includes(name)), 'Reopened workspace state must remain visible throughout the settling interval');
      assert.deepEqual(telemetry.get(reopened.page), { persistent: storageValue, session: storageValue, localStorage: storageValue });
      assert.equal(actionCount, 2, 'Reopening the workspace must not replay the state write');
      await sleep(120);
    } while (Date.now() < deadline);
    assert.equal(documentLoads.get('fresh-workspace'), 1);
    assert.equal(documentLoads.get('fresh-after-last-close'), 1);
    return { onlyWorkspaceTabClosed: true, otherWorkspaceStayedOpen: true, persistentCookieRetained: true,
      sessionCookieRetained: true, localStorageRetained: true, totalStateWrites: 2, repeatedWrites: 0, settlingIntervalMs: 1000 };
  });
} catch (error) {
  if (!results.some(result => !result.passed)) results.push({ name: 'Startup harness', passed: false, error: error.message });
  console.log(`FAIL startup harness: ${error.message}`);
} finally {
  for (const client of clients) await client.close().catch(() => {});
  if (browser) await stopSpawnedBrowser().catch(error => results.push({ name: 'Spawned process cleanup', passed: false, error: error.message }));
  if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
  try {
    applicationDllSha256AtEnd = await hashDll();
    assert.equal(applicationDllSha256AtEnd, applicationDllSha256, 'Application DLL changed during the startup run');
  } catch (error) { results.push({ name: 'Tested binary stability', passed: false, error: error.message }); }
  const report = { run, capturedAt: new Date().toISOString(), binary: 'build/app/Release/Xenon.exe', applicationDllSha256, applicationDllSha256AtEnd,
    profile, launches: launches.length, shutdownMode: 'native-menu-exit', firstExitCode, seededStartupPreferences, fixtureCounts: counts(),
    fixtureStates: Object.fromEntries(telemetry), lastFixtureObservations: Object.fromEntries(lastObservations),
    passed: results.length === 7 && results.every(result => result.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true });
  const reportPath = resolve(root, 'out/startup-integration-results.json');
  await writeFile(reportPath, JSON.stringify(report, null, 2));
  console.log(`Report: ${reportPath}`);
  process.exitCode = report.passed ? 0 : 1;
}
