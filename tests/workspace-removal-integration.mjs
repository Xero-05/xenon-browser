// Actual CEF + real MCP. Default removal uses the unshipped AuthTest native
// driver; --manual requires an actual human approving production native UI.
// Every profile, download, credential and approved source is synthetic.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { lstat, mkdir, readFile, readdir, writeFile } from 'node:fs/promises';
import { basename, resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';

const root = resolve(import.meta.dirname, '..');
const run = 'workspace-removal-' + Date.now() + '-' + randomBytes(4).toString('hex');
const profile = resolve(root, '.cache', 'auth-integration-removal-' + run);
const pipeName = 'xenon-test-' + run, pipe = '\\\\.\\pipe\\' + pipeName;
const manual = process.argv.includes('--manual');
const binaryRelative = manual ? 'build/app/Release/Xenon.exe' : 'build/auth-fixture/Release/XenonAuthTest.exe';
const binary = resolve(root, binaryRelative);
const applicationDll = resolve(root, manual ? 'build/app/Release/Xenon.dll' : 'build/auth-fixture/Release/XenonAuthTest.dll');
const configPath = resolve(profile, 'client-config.json');
const removedId = 'auth_fixture_shared', keptId = 'keep_' + randomBytes(8).toString('hex');
const profilePath = id => resolve(profile, 'workspace-' + createHash('sha256').update(id).digest('hex'));
const results = [], clients = [], launches = [], telemetry = new Map();
const runFile = promisify(execFile), sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
let browser, server, origin, applicationDllSha256, applicationDllSha256AtEnd, client, removedWorker, keptWorker, removedTab, keptTab;
let vaultSeed, downloadedFile, keptDocument, sourcePath, lastObservation, pendingReadLoop;
let stopPendingReads = false;
const readReplies = [];
const hashDll = async () => createHash('sha256').update(await readFile(applicationDll)).digest('hex');
const raw = (name, args = {}, target = client) => target.callTool({ name: 'xenon_' + name, arguments: args });
async function tool(name, args = {}, target = client) {
  const response = await raw(name, args, target);
  assert(!response.isError, name + ': ' + JSON.stringify(response.structuredContent ?? response.content));
  return response.structuredContent;
}
const workerScope = worker => ({ agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId });
const scope = tab => ({ agentSessionId: tab.agentSessionId, workspaceId: tab.workspaceId, tabId: tab.tabId });
const mutation = tab => ({ ...scope(tab), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID() });
const alivePipe = () => new Promise(resolve => {
  const socket = net.connect(pipe);
  socket.once('connect', () => { socket.destroy(); resolve(true); });
  socket.once('error', () => { socket.destroy(); resolve(false); });
});
async function until(action, timeout = 15000, label = 'Synthetic fixture state') {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) { const value = await action(); if (value) return value; await sleep(100); }
  throw new Error(label + ' timed out');
}
async function check(name, action) {
  const started = Date.now();
  try { const details = await action(); results.push({ name, passed: true, elapsedMs: Date.now() - started, details }); console.log('PASS ' + name); }
  catch (error) { results.push({ name, passed: false, elapsedMs: Date.now() - started, error: error.message }); throw error; }
}
async function observe(tab) {
  const deadline = Date.now() + 5000;
  for (;;) {
    const response = await raw('observe', { ...scope(tab), maxNodes: 300 }); lastObservation = response;
    if (response.isError && response.structuredContent?.error?.code === 'privacy_guard_initializing' && Date.now() < deadline) { await sleep(100); continue; }
    assert(!response.isError, 'observe: ' + JSON.stringify(response.structuredContent ?? response.content));
    return response.structuredContent;
  }
}
async function ready(tab, cookie, stored) {
  return until(async () => {
    const observation = await observe(tab), names = observation.nodes.map(node => node.name);
    return names.includes('Removal fixture ready') && names.includes('Cookie: ' + (cookie ?? '[empty]')) &&
      names.includes('Storage: ' + (stored ?? '[empty]')) ? observation : false;
  }, 15000, 'Visible persistent fixture state');
}
async function click(tab, label) {
  const observation = await observe(tab), element = observation.nodes.find(node => node.name === label && node.ref);
  assert(element, 'Visible control missing: ' + label);
  return tool('interact', { ...mutation(tab), observationId: observation.observationId, action: 'click', elementRef: element.ref });
}
async function open(worker, caseId) {
  return { ...worker, ...await tool('tab_create', { ...workerScope(worker), url: origin + '/page?case=' + caseId }), caseId };
}
async function launch() {
  assert.equal(await hashDll(), applicationDllSha256, 'DLL changed between launches');
  browser = spawn(binary, ['--user-data-dir=' + profile, '--broker-pipe=' + pipeName, ...(manual || launches.length ? [] : ['--test-native-removal'])], { windowsHide: true, stdio: 'ignore' });
  let launchError; browser.once('error', error => { launchError = error; }); launches.push(browser.pid);
  await until(async () => {
    if (launchError) throw launchError;
    if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Owned browser exited before pipe readiness');
    return alivePipe();
  }, 45000, 'Private browser pipe');
  client = new Client({ name: 'Synthetic workspace removal', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(new StdioClientTransport({ command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath], stderr: 'pipe' })); clients.push(client);
}
async function stopOwnedBrowser() {
  const target = browser; browser = undefined;
  if (target?.pid && target.exitCode === null && target.signalCode === null)
    await runFile('taskkill.exe', ['/PID', String(target.pid), '/T', '/F'], { windowsHide: true });
  if (target?.pid) await until(() => target.exitCode !== null || target.signalCode !== null, 10000, 'Owned PID exit');
  await until(async () => !(await alivePipe()), 15000, 'Private pipe removal');
}
async function descendants(path) {
  const files = [];
  for (const entry of await readdir(path, { withFileTypes: true })) {
    assert(!entry.isSymbolicLink(), 'Synthetic tree unexpectedly contains a link');
    const child = resolve(path, entry.name);
    if (entry.isDirectory()) files.push(...await descendants(child)); else if (entry.isFile()) files.push(child);
  }
  return files;
}
async function storageRemains() {
  const database = await readFile(resolve(profile, 'vault.sqlite3'));
  assert(database.length > 0);
  assert(!database.includes(Buffer.from('XENON_TEST_PASSWORD_CANARY_73ab!')), 'Vault exposed synthetic password plaintext');
  assert.equal(await readFile(sourcePath, 'utf8'), 'Xenon synthetic approved upload.\n');
  assert.equal(await readFile(downloadedFile, 'utf8'), 'Synthetic removal download retained.\n');
}
async function startFixture() {
  const cookie = request => (request.headers.cookie ?? '').split(';').map(value => value.trim()).find(value => value.startsWith('xenon_removal='))?.slice(14) ?? null;
  server = http.createServer((request, response) => {
    const url = new URL(request.url, origin ?? 'http://127.0.0.1'); response.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/set') {
      const value = url.searchParams.get('value');
      if (request.method !== 'POST' || !['remove', 'keep'].includes(value)) { response.writeHead(400).end(); return; }
      response.setHeader('Set-Cookie', 'xenon_removal=' + value + '; Max-Age=86400; Path=/; HttpOnly; SameSite=Lax'); response.end('ok'); return;
    }
    if (url.pathname === '/state') { response.setHeader('Content-Type', 'application/json'); response.end(JSON.stringify({ cookie: cookie(request) })); return; }
    if (url.pathname === '/download') { response.setHeader('Content-Type', 'text/plain'); response.setHeader('Content-Disposition', 'attachment; filename="removal-fixture.txt"'); response.end('Synthetic removal download retained.\n'); return; }
    if (url.pathname === '/telemetry' && request.method === 'POST') {
      let body = ''; request.on('data', chunk => { body += chunk; if (body.length > 4096) request.destroy(); });
      request.on('end', () => { try { telemetry.set(url.searchParams.get('case'), { ...JSON.parse(body), at: Date.now(), cookie: cookie(request) }); response.end('ok'); } catch { response.writeHead(400).end(); } }); return;
    }
    const caseId = url.searchParams.get('case');
    if (url.pathname !== '/page' || !['remove', 'keep', 'popup', 'reopened'].includes(caseId)) { response.writeHead(404).end(); return; }
    response.setHeader('Content-Type', 'text/html; charset=utf-8');
    response.end('<!doctype html><meta charset="utf-8"><title>Xenon Remove workspace fixture — ' + caseId + '</title>' +
      '<style>body{font:17px system-ui;color:#152235;background:white;margin:24px}button,a,input{font:inherit;padding:10px;margin:8px;color:#152235;background:white}p{margin:12px}</style>' +
      '<h1 id="ready">Loading fixture</h1><p id="cookie"></p><p id="stored"></p><label>Draft <input id="draft" value="Synthetic draft"></label>' +
      '<button id="write">Write workspace state</button><button id="increment">Increment surviving count</button><p id="count">Count: 0</p>' +
      '<button id="popup">Open removable popup</button><a href="/download" download>Download retained file</a>' +
      '<script>const caseId=' + JSON.stringify(caseId) + ';let count=0;const key="xenon-removal";' +
      'async function refresh(){const state=await(await fetch("/state")).json();const stored=localStorage.getItem(key);document.querySelector("#cookie").textContent="Cookie: "+(state.cookie??"[empty]");document.querySelector("#stored").textContent="Storage: "+(stored??"[empty]");document.querySelector("#ready").textContent="Removal fixture ready";await fetch("/telemetry?case="+caseId,{method:"POST",body:JSON.stringify({stored,count,timeOrigin:performance.timeOrigin,draft:document.querySelector("#draft").value})});}' +
      'document.querySelector("#write").onclick=async()=>{await fetch("/set?value="+caseId,{method:"POST"});localStorage.setItem(key,caseId);await refresh()};document.querySelector("#increment").onclick=()=>{document.querySelector("#count").textContent="Count: "+(++count);refresh()};' +
      'document.querySelector("#popup").onclick=()=>window.open("/page?case=popup","_blank");refresh();</script>');
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  origin = 'http://127.0.0.1:' + server.address().port;
}

if (process.platform !== 'win32') throw new Error('Removal regression requires Windows.');
if (await alivePipe()) throw new Error('Random test pipe already exists.');
try {
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_AUTH_FIXTURE\n');
  vaultSeed = JSON.parse((await runFile(resolve(root, 'build/Release/vault_fixture_seed.exe'), [profile], { windowsHide: true })).stdout.trim());
  sourcePath = resolve(profile + '-uploads', 'approved-report.txt');
  const identity = { clientId: 'remove_' + randomBytes(8).toString('hex'), token: randomBytes(32).toString('hex'), pipe };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic removal client', tokenHash: createHash('sha256').update(identity.token).digest('hex') }],
    workspaces: [removedId, keptId].map(id => ({ id, clients: [identity.clientId] })),
    accountGrants: [{ clientId: identity.clientId, workspaceId: removedId, accountId: vaultSeed.accountId, origin: vaultSeed.origin }], operations: [] }));
  await writePrivateConfig(configPath, identity); await startFixture(); applicationDllSha256 = await hashDll(); await launch();
  await check('Two isolated workspaces have live data, popup, vault grant, approved file and download', async () => {
    removedWorker = await tool('worker_create', { name: 'Remove synthetic workspace', workspaceId: removedId });
    keptWorker = await tool('worker_create', { name: 'Keep synthetic workspace', workspaceId: keptId });
    removedTab = await open(removedWorker, 'remove'); keptTab = await open(keptWorker, 'keep');
    await ready(removedTab, null, null); await ready(keptTab, null, null);
    await click(removedTab, 'Write workspace state'); await ready(removedTab, 'remove', 'remove');
    await ready(keptTab, null, null); await click(keptTab, 'Write workspace state'); keptDocument = (await ready(keptTab, 'keep', 'keep')).documentId;
    await click(removedTab, 'Open removable popup');
    const tabs = await until(async () => { const value = await tool('tabs', workerScope(removedWorker)); return value.tabs.length === 2 ? value.tabs : false; });
    assert(tabs.some(tab => tab.tabId !== removedTab.tabId && tab.controlGroupId === tabs.find(item => item.tabId === removedTab.tabId).controlGroupId));
    assert((await tool('folders', workerScope(removedWorker))).folders.some(item => item.folderId === vaultSeed.folderId));
    assert(JSON.parse(await readFile(resolve(profile, 'broker-state.json'), 'utf8')).accountGrants.some(item => item.accountId === vaultSeed.accountId && item.workspaceId === removedId));
    await click(removedTab, 'Download retained file');
    await until(async () => (await tool('downloads', workerScope(removedWorker))).downloads.some(item => item.complete || item.state === 'complete'));
    downloadedFile = await until(async () => (await descendants(resolve(profile, 'downloads'))).find(path => path.endsWith('-removal-fixture.txt')));
    await storageRemains();
    return { workspaces: 2, removableTabs: 2, relatedPopup: true, isolatedPersistentValues: true, encryptedVaultSeeded: true, approvedFileAndDownload: true };
  });
  await check('Native removal revokes access and pending reads while preserving unrelated live work', async () => {
    const oldEvidence = await observe(removedTab), oldButton = oldEvidence.nodes.find(node => node.name === 'Increment surviving count' && node.ref);
    assert(oldButton); const before = await until(() => telemetry.get('keep'));
    pendingReadLoop = (async () => {
      while (!stopPendingReads) {
        const response = await raw('wait', { ...scope(removedTab), agentSessionId: keptWorker.agentSessionId, text: 'Never appears synthetic pending read', timeoutMs: 15000 });
        readReplies.push(response);
        const code = response.structuredContent?.error?.code;
        if (response.isError && !['timeout', 'privacy_guard_initializing'].includes(code)) return;
      }
    })();
    if (manual) console.log('READY FOR NATIVE REMOVE ' + JSON.stringify({ pid: browser.pid, workspaceId: removedId, keepWorkspaceId: keptId,
      fixtureTitle: 'Xenon Remove workspace fixture — remove', action: 'Select auth_fixture_shared in this test instance Controls, Remove workspace, then Yes. No other workspace is authorized.', deadlineSeconds: 180 }));
    else {
      // This file is consumed only by the marked AuthTest build. There is no
      // production switch, renderer binding, pipe operation or MCP endpoint.
      await sleep(300);
      await writeFile(resolve(profile, 'native-removal-request.json'), JSON.stringify({ workspaceId: removedId }));
    }
    await until(async () => !(await tool('workspaces')).workspaces.some(item => item.workspaceId === removedId), 180000, 'Human native workspace removal');
    stopPendingReads = true; await pendingReadLoop;
    assert(readReplies.some(response => response.isError && response.structuredContent?.error?.code === 'ACCESS_REVOKED'), 'In-flight page read did not report scope revocation');
    if (!manual) {
      const outcome = await until(async () => { try { return JSON.parse(await readFile(resolve(profile, 'native-removal-result.json'), 'utf8')); } catch (error) { if (error.code === 'ENOENT' || error instanceof SyntaxError) return false; throw error; } });
      assert.equal(outcome.ok, true, 'Native removal driver did not complete removal');
    }
    for (const name of ['observe', 'tabs', 'folders', 'downloads', 'accounts']) {
      const args = ['observe', 'accounts'].includes(name) ? { ...scope(removedTab), agentSessionId: keptWorker.agentSessionId } : { agentSessionId: keptWorker.agentSessionId, workspaceId: removedId };
      const response = await raw(name, args); assert(response.isError, 'Removed scope remained available: ' + name);
      assert(!response.structuredContent?.nodes, 'Removed scope returned page nodes');
    }
    assert((await raw('worker_resume', { agentSessionId: removedWorker.agentSessionId })).isError);
    assert((await raw('worker_create', { name: 'Must stay removed', workspaceId: removedId })).isError);
    assert((await raw('interact', { ...mutation(removedTab), observationId: oldEvidence.observationId, action: 'click', elementRef: oldButton.ref })).isError);
    assert(!(await tool('workers')).workers.some(item => item.workspaceId === removedId && item.connected));
    const saved = JSON.parse(await readFile(resolve(profile, 'broker-state.json'), 'utf8'));
    assert(saved.removedWorkspaces.includes(removedId)); assert(!saved.workspaces.some(item => item.id === removedId));
    assert(!saved.accountGrants.some(item => item.workspaceId === removedId));
    assert.equal((await ready(keptTab, 'keep', 'keep')).documentId, keptDocument);
    assert.equal(telemetry.get('keep').timeOrigin, before.timeOrigin);
    await click(keptTab, 'Increment surviving count'); await until(() => telemetry.get('keep')?.count === 1);
    assert.equal(telemetry.get('keep').draft, 'Synthetic draft'); await storageRemains();
    return { nativeInvocation: manual ? 'human_ui' : 'unshipped_test_driver', tombstoneDurable: true, workerAndScopeRevoked: true, pendingReadDenied: true,
      staleMutationDenied: true, survivorDocumentUnchanged: true, survivorActionSucceeded: true, vaultAndExternalFilesRetained: true };
  });
  await check('Restart purges only the tombstoned profile and cannot recreate its grant', async () => {
    assert((await lstat(profilePath(removedId))).isDirectory(), 'Removed profile should await startup cleanup');
    console.log('Waiting 35 seconds for Chromium persistent storage flush before stopping only the synthetic PID.'); await sleep(35000);
    await client.close(); await stopOwnedBrowser();
    // Deterministically reproduce Chrome's stale initial-profile selection.
    // Only this generated profile is edited, while its owned browser is stopped.
    // The production fixed initial profile must override both retained hints.
    assert.equal(await readFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'utf8'), 'XENON_SYNTHETIC_AUTH_FIXTURE\n');
    const localStatePath = resolve(profile, 'Local State');
    const localState = JSON.parse(await readFile(localStatePath, 'utf8'));
    assert(localState.profile && typeof localState.profile === 'object' && !Array.isArray(localState.profile));
    localState.profile.last_used = basename(profilePath(removedId));
    localState.profile.last_active_profiles = [basename(profilePath(removedId))];
    await writeFile(localStatePath, JSON.stringify(localState));
    await launch();
    await assert.rejects(lstat(profilePath(removedId)), error => error.code === 'ENOENT');
    assert((await lstat(profilePath(keptId))).isDirectory());
    const saved = JSON.parse(await readFile(resolve(profile, 'broker-state.json'), 'utf8')); assert(saved.removedWorkspaces.includes(removedId));
    assert(!(await tool('workspaces')).workspaces.some(item => item.workspaceId === removedId));
    assert((await raw('worker_create', { name: 'Removed after restart', workspaceId: removedId })).isError);
    await storageRemains(); return { launches: launches.length, seededLastUsedRemovedProfile: true, seededLastActiveRemovedProfile: true,
      removedProfileAbsent: true, tombstonePermanent: true, otherProfilePresent: true, noRecreation: true };
  });
  await check('Remaining workspace cookie and local storage survive while shared files remain', async () => {
    const worker = await tool('worker_create', { name: 'Survivor after restart', workspaceId: keptId });
    const tab = await open(worker, 'reopened'); await ready(tab, 'keep', 'keep');
    assert.equal(telemetry.get('reopened')?.cookie, 'keep'); assert.equal(telemetry.get('reopened')?.stored, 'keep');
    await click(tab, 'Increment surviving count'); await until(() => telemetry.get('reopened')?.count === 1); await storageRemains();
    return { survivorCookieRetained: true, survivorLocalStorageRetained: true, actionAfterRestart: true, vaultDownloadAndApprovedSourceRetained: true };
  });
} catch (error) {
  if (!results.some(result => !result.passed)) results.push({ name: 'Removal harness', passed: false, error: error.message });
  console.log('FAIL removal harness: ' + error.message);
} finally {
  stopPendingReads = true;
  for (const entry of clients) await entry.close().catch(() => {});
  await pendingReadLoop?.catch(() => {});
  if (browser) await stopOwnedBrowser().catch(error => results.push({ name: 'Owned process cleanup', passed: false, error: error.message }));
  if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
  if (lastObservation) await writeFile(resolve(profile, 'last-observation.json'), JSON.stringify(lastObservation, null, 2));
  try { applicationDllSha256AtEnd = await hashDll(); assert.equal(applicationDllSha256AtEnd, applicationDllSha256, 'DLL changed during removal test'); }
  catch (error) { results.push({ name: 'Tested binary stability', passed: false, error: error.message }); }
  const report = { run, capturedAt: new Date().toISOString(), binary: binaryRelative, profile,
    applicationDllSha256, applicationDllSha256AtEnd, launches: launches.length, nativeApprovalRequired: manual,
    passed: results.length === 4 && results.every(result => result.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true }); const reportPath = resolve(root, 'out/workspace-removal-integration-results.json');
  await writeFile(reportPath, JSON.stringify(report, null, 2)); console.log('Report: ' + reportPath); process.exitCode = report.passed ? 0 : 1;
}
