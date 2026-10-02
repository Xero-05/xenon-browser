// Actual production CEF + MCP, two process launches and a generated profile.
// This fixture never accesses another browser profile, raw CDP or a test hook.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { lstat, mkdir, readFile, readdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';

const root = resolve(import.meta.dirname, '..');
const run = `persistence-integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const pipeName = `xenon-test-${run}`, pipePath = `\\\\.\\pipe\\${pipeName}`;
const profile = resolve(root, '.cache', run);
const binary = resolve(root, 'build/app/Release/Xenon.exe');
const applicationDll = resolve(root, 'build/app/Release/Xenon.dll');
const configPath = resolve(profile, 'client-config.json');
const flushDelayMs = 35_000;
const shutdownMode = process.argv.includes('--graceful') || process.env.XENON_PERSISTENCE_SHUTDOWN === 'graceful' ? 'graceful' : 'forced';
const runFile = promisify(execFile);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const hashDll = async () => createHash('sha256').update(await readFile(applicationDll)).digest('hex');
const results = [], clients = [], launches = [], telemetry = new Map();
const persistenceDiagnostics = [];
// These digests are used only in memory for comparison. Neither the encrypted
// keys nor their digests are included in diagnostics, errors, or reports.
let firstShutdownKeyDigest, firstShutdownAppBoundDigest;
let browser, server, base, applicationDllSha256, applicationDllSha256AtEnd;

async function capturePersistenceDiagnostics(phase) {
  const diagnostic = { phase, localStateReadable: false, keyPresent: false, keyUnchanged: false,
    appBoundKeyPresent: false, appBoundKeyUnchanged: false, cookieDatabases: [] };
  try {
    const state = JSON.parse(await readFile(resolve(profile, 'Local State'), 'utf8'));
    diagnostic.localStateReadable = true;
    const key = state.os_crypt?.encrypted_key;
    const appBoundKey = state.os_crypt?.app_bound_encrypted_key;
    const digest = typeof key === 'string' && key.length ? createHash('sha256').update(key).digest('hex') : undefined;
    const appBoundDigest = typeof appBoundKey === 'string' && appBoundKey.length ? createHash('sha256').update(appBoundKey).digest('hex') : undefined;
    diagnostic.keyPresent = digest !== undefined;
    diagnostic.appBoundKeyPresent = appBoundDigest !== undefined;
    if (phase === 'after-first-shutdown') {
      firstShutdownKeyDigest = digest;
      firstShutdownAppBoundDigest = appBoundDigest;
    }
    diagnostic.keyUnchanged = digest !== undefined && digest === firstShutdownKeyDigest;
    diagnostic.appBoundKeyUnchanged = appBoundDigest !== undefined && appBoundDigest === firstShutdownAppBoundDigest;
  } catch (error) {
    // Never serialize the parsed preferences or a native error message which
    // might include local paths/data. Unreadable diagnostics do not alter tests.
    diagnostic.localStateReadError = typeof error.code === 'string' ? error.code : 'UNREADABLE';
  }
  try {
    for (const entry of await readdir(profile, { withFileTypes: true })) {
      if (!entry.isDirectory() || entry.isSymbolicLink() || !/^workspace-[a-f0-9]{64}$/.test(entry.name)) continue;
      const record = { workspaceDirectory: entry.name, databasePresent: false };
      for (const [suffix, field] of [['', 'databaseBytes'], ['-journal', 'journalBytes'], ['-wal', 'walBytes']]) {
        try {
          const details = await lstat(resolve(profile, entry.name, 'Network', `Cookies${suffix}`));
          if (details.isFile() && !details.isSymbolicLink()) {
            record[field] = details.size;
            if (!suffix) record.databasePresent = true;
          }
        } catch (error) { if (error.code !== 'ENOENT') record.statError = typeof error.code === 'string' ? error.code : 'UNREADABLE'; }
      }
      diagnostic.cookieDatabases.push(record);
    }
  } catch (error) { diagnostic.directoryReadError = typeof error.code === 'string' ? error.code : 'UNREADABLE'; }
  persistenceDiagnostics.push(diagnostic);
}

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
    throw error; // Later stages depend on an honestly established first launch.
  }
}
async function tool(client, name, args = {}) {
  const reply = await client.callTool({ name: `xenon_${name}`, arguments: args });
  if (reply.isError) throw new Error(`${name}: ${JSON.stringify(reply.structuredContent ?? reply.content)}`);
  return reply.structuredContent;
}
const scope = tab => ({ agentSessionId: tab.agentSessionId, workspaceId: tab.workspaceId, tabId: tab.tabId });
const mutation = tab => ({ ...scope(tab), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID() });
async function evidence(client, tab, expectedCookie, expectedStorage) {
  let lastNames = [], lastError;
  try { return await until(async () => {
    try {
      const value = await tool(client, 'observe', { ...scope(tab), maxNodes: 300 });
      const names = value.nodes?.map(node => node.name) ?? [];
      lastNames = names; lastError = undefined;
      return names.includes('Persistence fixture ready') &&
        names.includes(`Cookie: ${expectedCookie ?? '[empty]'}`) &&
        names.includes(`Local storage: ${expectedStorage ?? '[empty]'}`) ? value : false;
    } catch (error) { lastError = error.message; return false; }
  }, 20_000, 'Expected persistent state in browser evidence'); }
  catch { throw new Error(`Expected persisted state for ${tab.caseId}: ${JSON.stringify({ lastNames: lastNames.slice(0, 30), lastError, telemetry: telemetry.get(tab.caseId) })}`); }
}
async function openPage(client, worker, caseId, writeValue) {
  const url = new URL('/page', base);
  url.searchParams.set('case', caseId);
  if (writeValue) url.searchParams.set('write', writeValue);
  return { ...worker, ...await tool(client, 'tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: url.href }), caseId };
}
async function writeState(client, tab, expected) {
  const observed = await evidence(client, tab, null, null);
  const button = observed.nodes.find(node => node.role === 'button' && node.name === 'Write persistent state' && node.ref);
  assert(button, 'Fixture write control must be observed before acting');
  await tool(client, 'interact', { ...mutation(tab), observationId: observed.observationId, action: 'click', elementRef: button.ref });
  await evidence(client, tab, expected, expected);
  await until(() => {
    const state = telemetry.get(tab.caseId);
    return state?.cookie === expected && state?.localStorage === expected;
  }, 10_000, 'Website confirmation of cookie and local storage');
}
async function connect() {
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath],
    stderr: 'pipe',
  });
  const client = new Client({ name: 'Xenon persistence regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(transport); clients.push(client); return client;
}
async function launch() {
  assert.equal(await hashDll(), applicationDllSha256, 'Application changed between process launches');
  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { windowsHide: true, stdio: 'ignore' });
  let launchError;
  browser.once('error', error => { launchError = error; });
  launches.push({ pid: browser.pid, startedAt: new Date().toISOString() });
  await until(async () => {
    if (launchError) throw launchError;
    if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Production browser exited before MCP became ready');
    return alivePipe();
  }, 45_000, 'Browser private pipe');
  return connect();
}
async function stopSpawnedBrowser() {
  const target = browser; browser = undefined;
  // Never kill by executable name or touch a pre-existing browser process.
  if (target?.pid && target.exitCode === null && target.signalCode === null)
    await runFile('taskkill.exe', ['/PID', String(target.pid), '/T', '/F'], { windowsHide: true });
  if (target?.pid) await until(() => target.exitCode !== null || target.signalCode !== null, 10_000, 'Spawned browser process exit');
  await until(async () => !(await alivePipe()), 15_000, 'Previous browser pipe shutdown');
}
async function closeAgentTabsAndWaitForNativeExit(client, workers) {
  for (const worker of workers) {
    const workerScope = { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId };
    const { tabs } = await tool(client, 'tabs', workerScope);
    for (const tab of tabs) {
      assert.equal(tab.ownerSessionId, worker.agentSessionId, 'Only close tabs owned by this synthetic worker');
      await tool(client, 'tab_close', mutation({ ...worker, ...tab }));
    }
    await until(async () => (await tool(client, 'tabs', workerScope)).tabs.length === 0, 15_000, 'Synthetic agent tab closure');
  }
  await client.close();
  console.log('READY FOR NATIVE CLOSE: Only this test instance\'s initial blank browser window remains. Close it normally now; waiting up to 180 seconds.');
  const target = browser;
  await until(() => target.exitCode !== null || target.signalCode !== null, 180_000, 'Native browser window graceful close');
  assert.equal(target.exitCode, 0, 'Normal native close must exit successfully');
  browser = undefined;
  await until(async () => !(await alivePipe()), 15_000, 'Graceful browser pipe shutdown');
}
function startFixture() {
  const valueOfCookie = req => {
    const entry = (req.headers.cookie ?? '').split(';').map(part => part.trim()).find(part => part.startsWith('xenon_persistent='));
    return entry ? decodeURIComponent(entry.slice('xenon_persistent='.length)) : null;
  };
  server = http.createServer((req, res) => {
    const url = new URL(req.url, base ?? 'http://127.0.0.1');
    res.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/set' && req.method === 'POST') {
      const value = url.searchParams.get('value') ?? '';
      if (!/^[a-z0-9_-]{1,100}$/.test(value)) { res.writeHead(400).end(); return; }
      res.setHeader('Set-Cookie', `xenon_persistent=${value}; Max-Age=86400; Path=/; HttpOnly; SameSite=Lax`);
      res.end('stored'); return;
    }
    if (url.pathname === '/state') {
      res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify({ cookie: valueOfCookie(req) })); return;
    }
    if (url.pathname === '/telemetry' && req.method === 'POST') {
      let body = '';
      req.on('data', chunk => { body += chunk; if (body.length > 4096) req.destroy(); });
      req.on('end', () => {
        try {
          const value = JSON.parse(body);
          telemetry.set(url.searchParams.get('case'), { cookie: valueOfCookie(req), localStorage: value.localStorage, at: Date.now() });
          res.end('ok');
        } catch { res.writeHead(400).end(); }
      }); return;
    }
    if (url.pathname !== '/page') { res.writeHead(404).end(); return; }
    const caseId = url.searchParams.get('case') ?? '', writeValue = url.searchParams.get('write') ?? '';
    if (!/^[a-z0-9_-]{1,100}$/.test(caseId) || (writeValue && !/^[a-z0-9_-]{1,100}$/.test(writeValue))) { res.writeHead(400).end(); return; }
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    res.end(`<!doctype html><meta charset="utf-8"><title>Persistence fixture</title>
<h1 id="ready">Loading persistence fixture</h1><p id="cookie"></p><p id="storage"></p>
${writeValue ? '<button id="write">Write persistent state</button>' : ''}
<script>
const caseId=${JSON.stringify(caseId)},writeValue=${JSON.stringify(writeValue)},storageKey='xenon-persistence-canary';
async function refresh(){const current=await(await fetch('/state',{cache:'no-store'})).json();const stored=localStorage.getItem(storageKey);
document.querySelector('#cookie').textContent='Cookie: '+(current.cookie??'[empty]');
document.querySelector('#storage').textContent='Local storage: '+(stored??'[empty]');
await fetch('/telemetry?case='+encodeURIComponent(caseId),{method:'POST',body:JSON.stringify({localStorage:stored})});
document.querySelector('#ready').textContent='Persistence fixture ready';}
document.querySelector('#write')?.addEventListener('click',async()=>{await fetch('/set?value='+encodeURIComponent(writeValue),{method:'POST'});localStorage.setItem(storageKey,writeValue);await refresh();});
refresh().catch(()=>{document.querySelector('#ready').textContent='Persistence fixture failed';});
</script>`);
  });
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => { base = `http://127.0.0.1:${server.address().port}`; resolve(); });
  });
}

if (process.platform !== 'win32') throw new Error('Persistent CEF regression requires Windows.');
if (await alivePipe()) throw new Error('The generated persistence test pipe is already in use. Start a fresh test run.');
try {
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_PERSISTENCE_FIXTURE\n');
  const identity = { clientId: `persistence_${randomBytes(8).toString('hex')}`, token: randomBytes(32).toString('hex'), pipe: pipePath };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic persistence client', tokenHash: createHash('sha256').update(identity.token).digest('hex') }],
    workspaces: [], accountGrants: [], operations: [] }));
  await writePrivateConfig(configPath, identity);
  await startFixture();
  applicationDllSha256 = await hashDll();
  const first = await launch();
  const primary = await tool(first, 'worker_create', { name: 'Primary persistent workspace' });
  const secondary = await tool(first, 'worker_create', { name: 'Isolated persistent workspace' });
  assert.notEqual(primary.workspaceId, secondary.workspaceId);
  const primaryValue = `primary_${randomBytes(12).toString('hex')}`, secondaryValue = `secondary_${randomBytes(12).toString('hex')}`;
  await check('Distinct workspaces isolate persistent cookies and local storage before restart', async () => {
    const a = await openPage(first, primary, 'initial-primary', primaryValue);
    const b = await openPage(first, secondary, 'initial-secondary', secondaryValue);
    await evidence(first, a, null, null); await evidence(first, b, null, null);
    await writeState(first, a, primaryValue);
    const independent = await openPage(first, secondary, 'secondary-after-primary-write');
    await evidence(first, independent, null, null);
    await writeState(first, b, secondaryValue);
    const unchanged = await openPage(first, primary, 'primary-after-secondary-write');
    await evidence(first, unchanged, primaryValue, primaryValue);
    return { workspaces: 2, cookie: 'persistent HttpOnly Max-Age=86400', localStorageKeyShared: true, valuesDistinct: true };
  });
  await check(`Stored state survives ${shutdownMode === 'graceful' ? 'normal native shutdown' : 'a forced process stop'} and a second launch`, async () => {
    console.log(`Waiting ${flushDelayMs / 1000}s for Chromium storage flush before ${shutdownMode === 'graceful' ? 'normal native close' : 'stopping only the spawned browser PID'}.`);
    await sleep(flushDelayMs);
    if (shutdownMode === 'graceful') await closeAgentTabsAndWaitForNativeExit(first, [primary, secondary]);
    else { await first.close(); await stopSpawnedBrowser(); }
    await capturePersistenceDiagnostics('after-first-shutdown');
    const second = await launch();
    await capturePersistenceDiagnostics('after-second-launch');
    const a = await tool(second, 'worker_create', { name: 'Reopened primary', workspaceId: primary.workspaceId });
    const b = await tool(second, 'worker_create', { name: 'Reopened isolated', workspaceId: secondary.workspaceId });
    assert.notEqual(a.agentSessionId, primary.agentSessionId); assert.notEqual(b.agentSessionId, secondary.agentSessionId);
    assert.equal(a.workspaceId, primary.workspaceId); assert.equal(b.workspaceId, secondary.workspaceId);
    const firstReopened = await openPage(second, a, 'reopened-primary');
    const secondReopened = await openPage(second, b, 'reopened-secondary');
    await evidence(second, firstReopened, primaryValue, primaryValue);
    await evidence(second, secondReopened, secondaryValue, secondaryValue);
    assert.equal(telemetry.get('reopened-primary')?.cookie, primaryValue);
    assert.equal(telemetry.get('reopened-secondary')?.cookie, secondaryValue);
    assert.equal(telemetry.get('reopened-primary')?.localStorage, primaryValue);
    assert.equal(telemetry.get('reopened-secondary')?.localStorage, secondaryValue);
    return { launches: launches.length, flushDelayMs, shutdownMode, newWorkerHandles: true, originalWorkspaceHandles: true,
      primaryCookieAndStorageRetained: true, secondaryCookieAndStorageRetained: true, noWritesDuringSecondLaunch: true };
  });
  await check('A new workspace after restart does not inherit either saved session', async () => {
    const second = clients.at(-1), fresh = await tool(second, 'worker_create', { name: 'Fresh after restart' });
    assert(![primary.workspaceId, secondary.workspaceId].includes(fresh.workspaceId));
    const tab = await openPage(second, fresh, 'fresh-after-restart');
    await evidence(second, tab, null, null);
    assert.equal(telemetry.get(tab.caseId)?.cookie, null); assert.equal(telemetry.get(tab.caseId)?.localStorage, null);
    return { freshCookieEmpty: true, freshLocalStorageEmpty: true };
  });
} catch (error) {
  if (!results.some(result => !result.passed)) results.push({ name: 'Persistence harness', passed: false, error: error.message });
  console.log(`FAIL persistence harness: ${error.message}`);
} finally {
  if (launches.length >= 2) await capturePersistenceDiagnostics('after-second-launch-validation');
  for (const client of clients) await client.close().catch(() => {});
  if (browser) await stopSpawnedBrowser().catch(error => results.push({ name: 'Spawned process cleanup', passed: false, error: error.message }));
  if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
  try {
    applicationDllSha256AtEnd = await hashDll();
    assert.equal(applicationDllSha256AtEnd, applicationDllSha256, 'Application DLL changed during the persistence run');
  } catch (error) { results.push({ name: 'Tested binary stability', passed: false, error: error.message }); }
  const report = { run, capturedAt: new Date().toISOString(), binary: 'build/app/Release/Xenon.exe', applicationDllSha256, applicationDllSha256AtEnd,
    profile, launches: launches.length, flushDelayMs, shutdownMode, persistenceDiagnostics, fixtureStates: Object.fromEntries(telemetry), passed: results.length >= 3 && results.every(result => result.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true });
  const reportPath = resolve(root, shutdownMode === 'graceful' ? 'out/persistence-integration-results.json' : 'out/persistence-crash-results.json');
  await writeFile(reportPath, JSON.stringify(report, null, 2));
  if (shutdownMode === 'forced' && report.passed)
    await writeFile(resolve(root, 'out/persistence-integration-results.json'), JSON.stringify(report, null, 2));
  console.log(`Report: ${reportPath}`);
  process.exitCode = report.passed ? 0 : 1;
}
