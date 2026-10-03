// Four real MCP clients, production broker/vault, separate test-only CEF binary.
// Synthetic profile and loopback certificate only; no existing browser data.
import assert from 'node:assert/strict';
import net from 'node:net';
import https from 'node:https';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';
import { PipeTransport } from '../adapter/dist/src/ipc.js';

const root = resolve(import.meta.dirname, '..');
const run = `auth-integration-${Date.now()}`;
const pipeName = `xenon-test-${run}`, pipePath = `\\\\.\\pipe\\${pipeName}`;
const profile = resolve(root, '.cache', run);
const binary = resolve(process.env.XENON_AUTH_TEST_BROWSER ?? resolve(root, 'build/auth-fixture/Release/XenonAuthTest.exe'));
const applicationDll = binary.replace(/\.exe$/i, '.dll');
const seedBinary = resolve(process.env.XENON_AUTH_TEST_SEED ?? resolve(root, 'build/Release/vault_fixture_seed.exe'));
const cert = await readFile(resolve(root, 'tests/fixtures/auth-cert.pem'));
const base = 'https://127.0.0.1:18766';
const workspaceId = 'auth_fixture_shared';
const canaries = ['XENON_TEST_USERNAME_CANARY_8e9a@example.invalid', 'XENON_TEST_PASSWORD_CANARY_73ab!',
  'XENON_TEST_OTP_CANARY_92df', 'fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210'];
const runFile = promisify(execFile);
const results = [], clients = [];
let browser, server, nativeTransport, applicationDllSha256, applicationDllSha256AtEnd, logCanaryScan;
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const alivePipe = () => new Promise(resolve => {
  const socket = net.connect(pipePath);
  socket.once('connect', () => { socket.destroy(); resolve(true); });
  socket.once('error', () => resolve(false));
});
async function until(fn, timeout = 20000, label = 'Authentication fixture condition') {
  const end = Date.now() + timeout;
  while (Date.now() < end) { const value = await fn(); if (value) return value; await sleep(100); }
  throw new Error(`${label} timed out`);
}
function fixture(path) {
  return new Promise((resolve, reject) => {
    https.get(base + path, { ca: cert }, response => {
      let body = ''; response.on('data', chunk => { body += chunk; }); response.on('end', () => resolve(body));
    }).on('error', reject);
  });
}
const telemetry = async id => JSON.parse(await fixture(`/telemetry?case=${encodeURIComponent(id)}`));
function safe(value) {
  const serialized = JSON.stringify(value);
  for (const secret of canaries) assert(!serialized.includes(secret), 'A synthetic credential canary crossed an MCP response boundary');
  return value;
}
async function scanNativeLogs(directory) {
  let filesScanned = 0, bytesScanned = 0;
  const entries = await readdir(directory, { withFileTypes: true });
  for (const entry of entries) {
    assert(!entry.isSymbolicLink(), 'Synthetic diagnostic scan encountered an unexpected filesystem link');
    const path = resolve(directory, entry.name);
    if (entry.isDirectory()) {
      const nested = await scanNativeLogs(path); filesScanned += nested.filesScanned; bytesScanned += nested.bytesScanned;
    } else if (entry.isFile() && (/\.log$/i.test(entry.name) || /^LOG(?:\.old)?$/i.test(entry.name))) {
      const bytes = await readFile(path);
      for (const secret of canaries) assert(!bytes.includes(Buffer.from(secret)) && !bytes.includes(Buffer.from(secret, 'utf16le')),
        'A synthetic credential canary was written to a native browser log');
      ++filesScanned; bytesScanned += bytes.length;
    }
  }
  return { passed: true, filesScanned, bytesScanned,
    scope: 'Entire synthetic-profile tree, *.log and LOG diagnostics; UTF-8 and UTF-16 canaries; unreadable directories fail verification' };
}
async function raw(client, name, args = {}) { return safe(await client.callTool({ name: `xenon_${name}`, arguments: args })); }
async function tool(client, name, args = {}) {
  const response = await raw(client, name, args);
  if (response.isError) throw new Error(`${name} failed: ${JSON.stringify(response.structuredContent ?? response.content)}`);
  return response.structuredContent;
}
const scope = (tab, worker = tab) => ({ agentSessionId: worker.agentSessionId, workspaceId, tabId: tab.tabId });
const mutation = tab => ({ ...scope(tab), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID() });
const observe = (client, tab, worker = tab) => tool(client, 'observe', { ...scope(tab, worker), maxNodes: 1000 });
async function pageReady(client, tab, expected) {
  return until(async () => {
    try { const result = await observe(client, tab); return !result.loading && result.nodes?.some(n => n.name === expected) ? result : false; }
    catch { return false; }
  });
}
async function check(name, fn) {
  const start = Date.now();
  try { const details = await fn(); results.push({ name, passed: true, elapsedMs: Date.now() - start, details }); console.log(`PASS ${name}`); }
  catch (error) { results.push({ name, passed: false, elapsedMs: Date.now() - start, error: error.message }); console.log(`FAIL ${name}: ${error.message}`); }
}
async function expectSealed(client, worker, tab) {
  for (const action of ['observe', 'screenshot']) {
    const result = await raw(client, action, scope(tab, worker));
    assert(result.isError, `${action} exposed a protected authentication document`);
    assert(['SENSITIVE_AUTH_IN_PROGRESS', 'protected_auth', 'screenshot_protected'].includes(result.structuredContent?.error?.code), `${action} failed for an unrelated reason: ${result.structuredContent?.error?.code ?? 'missing error code'}`);
    assert(!result.content?.some(c => c.type === 'image'), 'Protected screenshot returned image bytes');
  }
  safe(await tool(client, 'tabs', { agentSessionId: worker.agentSessionId, workspaceId }));
}
if (process.platform !== 'win32') throw new Error('Real credential tests require Windows DPAPI and CEF.');
if (await alivePipe()) throw new Error('The generated credential test pipe is already in use. Start a fresh test run.');
await mkdir(profile, { recursive: true });
await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_AUTH_FIXTURE\n');
const seeded = JSON.parse((await runFile(seedBinary, [profile], { windowsHide: true })).stdout);
assert.equal(seeded.origin, base);
const identities = Array.from({ length: 4 }, (_, i) => ({ clientId: `auth_fixture_client_${i}`, token: randomBytes(32).toString('hex'), pipe: pipePath }));
await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({
  version: 1,
  clients: identities.map((c, i) => ({ id: c.clientId, name: `Auth fixture ${i}`, tokenHash: createHash('sha256').update(c.token).digest('hex') })),
  workspaces: [{ id: workspaceId, clients: identities.map(c => c.clientId) }],
  accountGrants: identities.slice(0, 3).map(c => ({ clientId: c.clientId, workspaceId, accountId: seeded.accountId, origin: seeded.origin })),
  operations: [],
}));
for (let i = 0; i < identities.length; ++i) await writePrivateConfig(resolve(profile, `client-${i}.json`), identities[i]);
try {
  server = spawn(process.execPath, [resolve(root, 'tests/fixtures/auth-server.mjs')], { windowsHide: true, stdio: 'ignore' });
  applicationDllSha256 = createHash('sha256').update(await readFile(applicationDll)).digest('hex');
  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { windowsHide: true, stdio: 'ignore' });
  let launchError;
  for (const child of [server, browser]) child.on('error', error => { launchError = error; });
  await until(async () => { if (launchError) throw launchError; return alivePipe(); }, 45000);
  await until(async () => { try { return await fixture('/health') === 'ok'; } catch { return false; } });
  for (let i = 0; i < identities.length; ++i) {
    const transport = new StdioClientTransport({ command: process.execPath, args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', resolve(profile, `client-${i}.json`)], stderr: 'pipe' });
    const client = new Client({ name: `Credential fixture client ${i}`, version: '1' }, { versionNegotiation: { mode: i % 2 ? 'legacy' : { pin: '2026-07-28' } } });
    await client.connect(transport); clients.push(client);
  }
  const workers = await Promise.all(clients.map((client, i) => tool(client, 'worker_create', { name: `Credential worker ${i}`, workspaceId })));
  const create = async (index, path) => ({ ...workers[index], ...await tool(clients[index], 'tab_create', { agentSessionId: workers[index].agentSessionId, workspaceId: workers[index].workspaceId, url: base + path }) });
  const safeTab = await create(0, '/safe?case=baseline-input');
  await pageReady(clients[0], safeTab, 'Safe visual fixture');
  await check('Safe page screenshots remain available', async () => {
    const image = await raw(clients[0], 'screenshot', scope(safeTab));
    assert(!image.isError);
    const content = image.content.find(c => c.type === 'image');
    assert(content?.data.length > 100 && content.mimeType === 'image/png');
    assert.equal(Buffer.from(content.data, 'base64').subarray(0, 8).toString('hex'), '89504e470d0a1a0a');
    await writeFile(resolve(profile, 'safe-page.png'), Buffer.from(content.data, 'base64'));
  });
  await check('No raw eval, CDP, cookies or storage tools', async () => {
    const tools = await clients[0].listTools();
    assert(!tools.tools.some(t => /evaluate|javascript|devtools|cookie|storage|clipboard/i.test(t.name)));
    for (const key of ['Ctrl+V', 'Ctrl+Shift+I', 'Alt+F4']) {
      const evidence = await observe(clients[0], safeTab);
      const result = await raw(clients[0], 'interact', { ...mutation(safeTab), observationId: evidence.observationId, action: 'key', key });
      assert(result.isError, 'Unsafe keyboard shortcut accepted');
    }
  });
  await check('Synthetic password and OTP input events seal all observers through reveal and handoff', async () => {
    for (const kind of ['password', 'otp']) {
      const id = `synthetic-input-${kind}`;
      const tab = await create(0, `/human-auth-sim?case=${id}&kind=${kind}`);
      const evidence = await pageReady(clients[0], tab, 'Synthetic auth input fixture');
      const button = evidence.nodes.find(node => node.ref && node.role === 'button' && node.name === 'Simulate authentication input');
      assert(button, 'Synthetic input control unavailable');
      const clicked = await raw(clients[0], 'interact', { ...mutation(tab), observationId: evidence.observationId, action: 'click', elementRef: button.ref });
      try {
        await until(async () => (await telemetry(id)).events.some(event => event.type === 'fixture-revealed'), 5000, `${kind} synthetic input/reveal after click ${JSON.stringify(clicked.structuredContent)}`);
      } catch (error) {
        throw new Error(`${error.message}; events=${JSON.stringify((await telemetry(id)).events)}; control=${JSON.stringify(await tool(clients[0], 'control_status', scope(tab)))}`);
      }
      await until(async () => (await tool(clients[0], 'control_status', scope(tab))).protected, 20000, `${kind} native quarantine`);
      await Promise.all(clients.map((client, i) => expectSealed(client, workers[i], tab)));
      let owner = 0;
      for (const recipient of [1, 2, 3, 0]) {
        const moved = await tool(clients[owner], 'control', { ...scope(tab), action: 'handoff', expectedGeneration: tab.ownershipGeneration, toSessionId: workers[recipient].agentSessionId });
        Object.assign(tab, workers[recipient], moved); owner = recipient;
        assert.equal(moved.protected, true);
        await Promise.all(clients.map((client, i) => expectSealed(client, workers[i], tab)));
      }
    }
    return { inputPath: 'synthetic DOM input event, not Windows manual input', passwordAndOtp: true, laterRevealProtected: true, handoffs: 8 };
  });
  await check('Native folder grants persist and upload only approved opaque file handles', async () => {
    const folders = await tool(clients[0], 'folders', { agentSessionId: workers[0].agentSessionId, workspaceId });
    assert(folders.folders.some(folder => folder.folderId === seeded.folderId && folder.available));
    const listing = await tool(clients[0], 'files', { agentSessionId: workers[0].agentSessionId, workspaceId, folderId: seeded.folderId, limit: 100 });
    const file = listing.files.find(file => file.name === 'approved-report.txt');
    assert(file?.fileId);
    assert(!listing.files.some(file => file.name === 'meeting-notes.txt'), 'Disguised pairing credentials were exposed by the folder grant');
    assert(!JSON.stringify(listing).includes(profile), 'Folder listing returned an absolute local path');
    const tab = await create(0, '/upload?case=approved-upload');
    const evidence = await pageReady(clients[0], tab, 'Approved upload fixture');
    const input = evidence.nodes.find(node => node.ref && node.tag === 'INPUT' && node.inputType === 'file');
    assert(input, 'No observed upload control');
    const unapproved = await raw(clients[0], 'upload', { ...mutation(tab), observationId: evidence.observationId, elementRef: input.ref, fileId: 'C:\\private\\vault.sqlite3' });
    assert(unapproved.isError, 'Raw path bypassed file capabilities');
    const fresh = await observe(clients[0], tab);
    const freshInput = fresh.nodes.find(node => node.ref && node.tag === 'INPUT' && node.inputType === 'file');
    assert(freshInput, 'Fresh upload input reference unavailable');
    await tool(clients[0], 'upload', { ...mutation(tab), observationId: fresh.observationId, elementRef: freshInput.ref, fileId: file.fileId });
    await until(async () => (await telemetry('approved-upload')).uploadAccepted);
    await tool(clients[0], 'tab_close', mutation(tab));
    return { persistedNativeFolder: true, serverVerifiedUpload: true, rawPathRejected: true, disguisedPairingConfigExcluded: true };
  });
  const unauthTab = await create(0, '/login?case=rights&mode=hold');
  await pageReady(clients[0], unauthTab, 'Authentication fixture ready');
  await check('Account rights remain per client across handoff', async () => {
    const permitted = await tool(clients[0], 'accounts', scope(unauthTab));
    assert(permitted.accounts.some(a => a.accountId === seeded.accountId));
    const restricted = await tool(clients[3], 'accounts', scope(unauthTab, workers[3]));
    assert.deepEqual(restricted.accounts, []);
    const moved = await tool(clients[0], 'control', { ...scope(unauthTab), action: 'handoff', expectedGeneration: unauthTab.ownershipGeneration, toSessionId: workers[3].agentSessionId });
    Object.assign(unauthTab, workers[3], moved);
    const evidence = await observe(clients[3], unauthTab);
    const denied = await raw(clients[3], 'login', { ...mutation(unauthTab), observationId: evidence.observationId, accountId: seeded.accountId });
    assert(denied.isError, 'Handoff granted credentials to an unapproved client');
    assert.equal((await telemetry('rights')).attempts, 0);
  });
  await check('Exact credential origin enforced', async () => {
    const wrong = { ...workers[0], ...await tool(clients[0], 'tab_create', { agentSessionId: workers[0].agentSessionId, workspaceId: workers[0].workspaceId, url: 'https://localhost:18766/login?case=wrong-origin&mode=hold' }) };
    const evidence = await pageReady(clients[0], wrong, 'Authentication fixture ready');
    assert.deepEqual((await tool(clients[0], 'accounts', scope(wrong))).accounts, []);
    const result = await raw(clients[0], 'login', { ...mutation(wrong), observationId: evidence.observationId, accountId: seeded.accountId });
    assert(result.isError, 'A credential was filled into a different hostname');
    assert.equal((await telemetry('wrong-origin')).attempts, 0);
  });
  await check('GET and readonly forms reject credentials before input or submission', async () => {
    nativeTransport = new PipeTransport(pipePath);
    await nativeTransport.connect(identities[0]);
    const nativeTool = async (method, args) => {
      const reply = safe(await nativeTransport.call(method, args));
      assert(reply.ok, `${method} failed in the native negative-case setup: ${JSON.stringify(reply.error)}`);
      return reply.result;
    };
    const nativeWorker = await nativeTool('workers.create', { name: 'Native fill-only regression worker', workspaceId });
    for (const submit of [true, false]) for (const kind of ['get', 'readonly-username', 'readonly-password']) {
      const id = `rejected-${kind}-${submit ? 'submit' : 'fill-only'}`;
      const path = kind === 'get' ? `/get-login?case=${id}` : `/readonly-login?case=${id}&field=${kind.replace('readonly-', '')}`;
      const tab = submit ? await create(0, path) : { ...nativeWorker, ...await nativeTool('tabs.create', { ...nativeWorker, url: base + path }) };
      const evidence = submit ? await pageReady(clients[0], tab, 'Authentication fixture ready') : await until(async () => {
        const response = safe(await nativeTransport.call('page.observe', { ...scope(tab), maxNodes: 1000 }));
        if (!response.ok && response.error?.code === 'privacy_guard_initializing') return false;
        assert(response.ok, `Native observation failed: ${JSON.stringify(response.error)}`);
        const observation = response.result;
        return observation.nodes?.some(node => node.name === 'Authentication fixture ready') ? observation : false;
      });
      const args = { ...mutation(tab), observationId: evidence.observationId, accountId: seeded.accountId };
      if (submit) {
        const result = await raw(clients[0], 'login', args);
        assert(result.isError, `${kind} received a credential through MCP`);
        assert.equal(result.structuredContent?.error?.code, 'auth_unavailable', `${kind} did not reach the native form guard`);
      } else {
        // The MCP schema deliberately has no submit override. Exercise the
        // same authenticated native broker boundary's fill-only code path.
        const result = safe(await nativeTransport.call('auth.login', { ...args, submit: false }));
        assert(!result.ok, `${kind} received a credential through native fill-only`);
        assert.equal(result.error?.code, 'auth_unavailable', `${kind} fill-only did not reach the native form guard`);
      }
      await sleep(100);
      const state = await telemetry(id);
      assert.equal(state.attempts, 0, `${kind} submitted a POST form`);
      assert.equal(state.getSubmissions, 0, `${kind} submitted a GET form`);
      assert.equal(state.credentialQueryReceived, false, `${kind} put a credential into a URL`);
      assert(!state.events.some(event => ['input', 'change', 'submit'].includes(event.type)), `${kind} injected field or submission events`);
      safe(await tool(clients[0], 'tabs', { agentSessionId: workers[0].agentSessionId, workspaceId }));
    }
    return { rejectedCases: 6, mcpSubmit: true, nativeFillOnly: true, injectedEvents: 0, submissions: 0, credentialQueries: 0 };
  });
  let protectedTab;
  await check('Opaque saved login authenticates without a model-visible credential', async () => {
    protectedTab = await create(0, '/login?case=protected&mode=hold');
    const evidence = await pageReady(clients[0], protectedTab, 'Authentication fixture ready');
    let settledLogin;
    const loginPromise = raw(clients[0], 'login', { ...mutation(protectedTab), observationId: evidence.observationId, accountId: seeded.accountId }).then(result => { settledLogin = result; return result; });
    // Admission and CEF callbacks have no fixed wall-clock deadline. Wait for
    // native quarantine, while still failing any denied login without retrying it.
    await until(async () => {
      assert(!settledLogin?.isError, `Login failed before quarantine: ${settledLogin?.structuredContent?.error?.code ?? 'missing error code'}`);
      return (await tool(clients[0], 'control_status', scope(protectedTab))).protected;
    }, 20000, 'Native login quarantine');
    await Promise.all(clients.map((client, i) => expectSealed(client, workers[i], protectedTab)));
    const loginResult = await loginPromise;
    assert(!loginResult.isError, JSON.stringify(loginResult.structuredContent));
    await until(async () => (await telemetry('protected')).authenticated);
    assert.equal((await telemetry('protected')).attempts, 1);
    return { authenticationVerifiedByFixture: true, credentialBytesReturned: false };
  });
  await check('Four-agent handoff preserves quarantine and sends no website input', async () => {
    assert(protectedTab, 'Protected fixture did not initialize');
    const baseline = await telemetry('protected');
    const initial = await tool(clients[0], 'control_status', scope(protectedTab));
    let owner = 0;
    for (const recipient of [1, 2, 3, 0]) {
      const moved = await tool(clients[owner], 'control', { ...scope(protectedTab), action: 'handoff', expectedGeneration: protectedTab.ownershipGeneration, toSessionId: workers[recipient].agentSessionId });
      Object.assign(protectedTab, workers[recipient], moved); owner = recipient;
      assert.equal(moved.tabId, initial.tabId);
      assert.equal(moved.protected, true);
      await Promise.all(clients.map((client, i) => expectSealed(client, workers[i], protectedTab)));
    }
    const after = await telemetry('protected');
    assert.equal(after.attempts, baseline.attempts);
    assert.deepEqual(after.events, baseline.events, 'Handoff injected website input, focus, or submit events');
    return { handoffs: 4, addedWebsiteEvents: 0, protectionPreserved: true };
  });
  await check('Username-first forms reuse only the exact-origin account', async () => {
    const tab = await create(1, '/username-first?case=username-first&mode=hold');
    const evidence = await pageReady(clients[1], tab, 'Username-first fixture');
    const result = await raw(clients[1], 'login', { ...mutation(tab), observationId: evidence.observationId, accountId: seeded.accountId });
    assert(!result.isError, JSON.stringify(result.structuredContent));
    await until(async () => (await telemetry('username-first')).authenticated);
    const state = await telemetry('username-first');
    assert(state.usernameAccepted && state.attempts === 1);
    await expectSealed(clients[1], workers[1], tab);
    return { usernameStep: true, passwordStep: true, oneSubmission: true };
  });
} catch (error) {
  results.push({ name: 'Authentication harness', passed: false, error: error.message });
  console.log(`FAIL harness: ${error.message}`);
} finally {
  for (const client of clients) await client.close().catch(() => {});
  nativeTransport?.close();
  if (browser?.pid) await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
  server?.kill();
  try {
    applicationDllSha256AtEnd = createHash('sha256').update(await readFile(applicationDll)).digest('hex');
    assert.equal(applicationDllSha256AtEnd, applicationDllSha256, 'Application DLL changed during the live test run');
  } catch (error) {
    results.push({ name: 'Tested binary stability', passed: false, error: error.message });
  }
  try { logCanaryScan = await scanNativeLogs(profile); }
  catch (error) {
    logCanaryScan = { passed: false };
    results.push({ name: 'Native log credential boundary', passed: false, error: error.message });
  }
  const report = { run, capturedAt: new Date().toISOString(), binary, applicationDll, applicationDllSha256, applicationDllSha256AtEnd, profile,
    passed: results.length > 0 && results.every(r => r.passed), logCanaryScan, results };
  await mkdir(resolve(root, 'out'), { recursive: true });
  await writeFile(resolve(root, 'out/auth-integration-results.json'), JSON.stringify(report, null, 2));
  console.log(`Report: ${resolve(root, 'out/auth-integration-results.json')}`);
  process.exitCode = report.passed ? 0 : 1;
}
