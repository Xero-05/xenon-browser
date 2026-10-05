// Actual native autofill API + CEF + real MCP observers. The file driver exists
// only in the unshipped AuthTest binary and a newly marked disposable profile.
// This tests the native path, not physical clicking of the account picker UI.
import assert from 'node:assert/strict';
import https from 'node:https';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { mkdir, readFile, readdir, rename, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';

const root = resolve(import.meta.dirname, '..');
const run = 'human-autofill-' + Date.now() + '-' + randomBytes(4).toString('hex');
const profile = resolve(root, '.cache', 'auth-integration-autofill-' + run);
const pipeName = 'xenon-test-' + run, pipe = '\\\\.\\pipe\\' + pipeName;
const binaryRelative = 'build/auth-fixture/Release/XenonAuthTest.exe';
const binary = resolve(root, binaryRelative), dll = binary.replace(/\.exe$/i, '.dll');
const origin = 'https://127.0.0.1:18766', otherOrigin = 'https://localhost:18766', workspaceId = 'auth_fixture_shared';
const username = 'XENON_TEST_USERNAME_CANARY_8e9a@example.invalid', password = 'XENON_TEST_PASSWORD_CANARY_73ab!';
const canaries = [username, password];
const results = [], clients = [], workers = [], states = new Map(), commands = new Map();
const runFile = promisify(execFile), sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
let server, browser, seed, requestId = 0, applicationDllSha256, applicationDllSha256AtEnd, lastNativeReply, logCanaryScan;
const hashDll = async () => createHash('sha256').update(await readFile(dll)).digest('hex');
const safe = value => { const text = JSON.stringify(value); for (const secret of canaries) assert(!text.includes(secret), 'Synthetic credential crossed a native diagnostic or MCP boundary'); return value; };
const scope = (tab, worker = tab) => ({ agentSessionId: worker.agentSessionId, workspaceId, tabId: tab.tabId });
const mutation = tab => ({ ...scope(tab), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID() });
const raw = async (index, name, args = {}) => safe(await clients[index].callTool({ name: 'xenon_' + name, arguments: args }));
async function tool(index, name, args = {}) {
  const reply = await raw(index, name, args); assert(!reply.isError, name + ': ' + JSON.stringify(reply.structuredContent ?? reply.content)); return reply.structuredContent;
}
async function until(action, timeout = 15000, label = 'Synthetic autofill condition') {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) { const value = await action(); if (value) return value; await sleep(100); }
  throw new Error(label + ' timed out');
}
const alivePipe = () => new Promise(resolve => {
  const socket = net.createConnection(pipe);
  socket.once('connect', () => { socket.destroy(); resolve(true); }); socket.once('error', () => { socket.destroy(); resolve(false); });
});
async function check(name, action) {
  const started = Date.now();
  try { const details = await action(); results.push({ name, passed: true, elapsedMs: Date.now() - started, details }); console.log('PASS ' + name); }
  catch (error) { results.push({ name, passed: false, elapsedMs: Date.now() - started, error: error.message }); throw error; }
}
async function native(action, params = {}) {
  const id = ++requestId;
  const temporary = resolve(profile, 'native-autofill-request.tmp');
  await writeFile(temporary, JSON.stringify({ id, action, ...params }));
  await rename(temporary, resolve(profile, 'native-autofill-request.json'));
  return until(async () => {
    let reply; try { reply = JSON.parse(await readFile(resolve(profile, 'native-autofill-result.json'), 'utf8')); } catch (error) { if (error.code === 'ENOENT' || error instanceof SyntaxError) return false; throw error; }
    if (reply.id !== id) return false;
    lastNativeReply = safe(reply); return reply;
  }, 20000, 'Native autofill ' + action);
}
async function offer(tab) {
  const reply = await native('request', { tabId: tab.tabId }); assert(reply.ok, 'Native offer rejected: ' + JSON.stringify(reply));
  assert.equal(reply.result.tabId, tab.tabId); assert.equal(reply.result.origin, origin);
  assert(reply.result.offerId); assert(reply.result.accounts.some(account => account.accountId === seed.accountId));
  return reply.result;
}
async function fill(offerValue) {
  const reply = await native('fill', { offerId: offerValue.offerId, accountId: seed.accountId });
  assert(reply.ok, 'Native fill rejected: ' + JSON.stringify(reply));
  assert.equal(reply.result.status, 'filled'); assert.equal(reply.result.submitted, false); return reply;
}
const rejectedAs = (reply, code) => { assert.equal(reply.ok, false); assert.equal(reply.error?.code, code); };
async function open(caseId, phase = 'credentials', targetOrigin = origin) {
  const tab = { ...workers[0], ...await tool(0, 'tab_create', { agentSessionId: workers[0].agentSessionId, workspaceId,
    url: targetOrigin + '/page?case=' + caseId + '&phase=' + phase }), caseId };
  await until(() => states.get(caseId)?.ready, 15000, 'Autofill fixture document load');
  // Only readiness errors may be retried. Native fill is always attempted once.
  await until(async () => {
    const reply = await raw(0, 'observe', { ...scope(tab), maxNodes: 100 });
    if (reply.isError && reply.structuredContent?.error?.code === 'privacy_guard_initializing') return false;
    assert(!reply.isError, 'Fixture readiness: ' + JSON.stringify(reply.structuredContent));
    return reply.structuredContent.nodes.some(node => node.name === 'Human autofill fixture');
  });
  return tab;
}
async function expectSealed(tab) {
  for (let index = 0; index < clients.length; ++index) {
    for (const action of ['observe', 'screenshot']) {
      const reply = await raw(index, action, scope(tab, workers[index]));
      assert(reply.isError, action + ' exposed a native autofilled document');
      assert(['SENSITIVE_AUTH_IN_PROGRESS', 'protected_auth', 'screenshot_protected'].includes(reply.structuredContent?.error?.code), 'Protected evidence failed for unrelated reason');
      assert(!reply.content?.some(item => item.type === 'image'), 'Protected page returned image bytes');
    }
  }
}
async function command(caseId, action, params = {}) {
  const value = { id: (commands.get(caseId)?.id ?? 0) + 1, action, ...params }; commands.set(caseId, value);
  if (action !== 'navigate') await until(() => states.get(caseId)?.commandId === value.id, 5000, 'Fixture DOM change');
}
const empty = caseId => { const state = states.get(caseId); assert.equal(state.usernameAccepted, false); assert.equal(state.passwordAccepted, false); assert.equal(state.inputs, 0); assert.equal(state.changes, 0); assert.equal(state.submissions, 0); assert.equal(state.networkSubmissions, 0); };
async function scanLogs(directory) {
  let filesScanned = 0, bytesScanned = 0;
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    assert(!entry.isSymbolicLink(), 'Unexpected filesystem link in synthetic log scan');
    const path = resolve(directory, entry.name);
    if (entry.isDirectory()) { const nested = await scanLogs(path); filesScanned += nested.filesScanned; bytesScanned += nested.bytesScanned; }
    else if (entry.isFile() && (/\.log$/i.test(entry.name) || /^LOG(?:\.old)?$/i.test(entry.name))) {
      const bytes = await readFile(path); for (const secret of canaries) assert(!bytes.includes(Buffer.from(secret)) && !bytes.includes(Buffer.from(secret, 'utf16le')), 'Synthetic autofill credential entered native logs');
      filesScanned++; bytesScanned += bytes.length;
    }
  }
  return { passed: true, filesScanned, bytesScanned };
}
async function startFixture() {
  const key = await readFile(resolve(root, 'tests/fixtures/auth-key.pem')), cert = await readFile(resolve(root, 'tests/fixtures/auth-cert.pem'));
  server = https.createServer({ key, cert }, (request, response) => {
    const url = new URL(request.url, origin), caseId = url.searchParams.get('case');
    response.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/command') { response.setHeader('Content-Type', 'application/json'); response.end(JSON.stringify(commands.get(caseId) ?? null)); return; }
    if (url.pathname === '/telemetry' && request.method === 'POST') {
      let body = ''; request.on('data', chunk => { body += chunk; if (body.length > 4096) request.destroy(); });
      request.on('end', () => {
        try {
          const payload = JSON.parse(body); for (const secret of canaries) assert(!body.includes(secret));
          const previous = states.get(caseId) ?? {};
          // Separate fetches can arrive out of order. Keep the latest captured
          // DOM state, not whichever older event packet arrived last.
          if (previous.timeOrigin !== payload.timeOrigin || (payload.telemetrySequence ?? 0) > (previous.telemetrySequence ?? -1))
            states.set(caseId, { ...previous, ...payload, networkSubmissions: previous.networkSubmissions ?? 0 });
          response.end('ok');
        } catch { response.writeHead(400).end(); }
      }); return;
    }
    if (url.pathname === '/submit') {
      const state = states.get(caseId) ?? {}; state.networkSubmissions = (state.networkSubmissions ?? 0) + 1; states.set(caseId, state);
      request.resume(); response.writeHead(400).end('Fixture must not submit'); return;
    }
    if (url.pathname !== '/page' || !/^[a-z0-9-]{1,80}$/.test(caseId ?? '')) { response.writeHead(404).end(); return; }
    const phase = url.searchParams.get('phase') ?? 'credentials';
    if (!['credentials', 'username', 'password', 'unowned-username', 'unowned-password', 'explicit-username'].includes(phase)) { response.writeHead(400).end(); return; }
    const unowned = phase.startsWith('unowned-'), usernameOnly = phase.endsWith('username') && phase !== 'explicit-username', passwordOnly = phase.endsWith('password');
    const userField = '<label>CWL Login Name <input id="j_username" name="j_username" type="text"' + (usernameOnly || phase === 'explicit-username' ? ' autocomplete="username webauthn"' : '') + '></label>';
    const passwordField = '<label>Password <input id="j_password" name="j_password" type="password" autocomplete="current-password"></label>';
    response.setHeader('Content-Type', 'text/html; charset=utf-8');
    response.end('<!doctype html><meta charset="utf-8"><title>Xenon Human Autofill — ' + caseId + '</title>' +
      '<style>body{font:18px system-ui;color:#152235;background:white;margin:24px}label{display:block;margin:18px 0}input,button{font:inherit;color:#152235;background:white;padding:10px;margin:8px}</style>' +
      '<h1>Human autofill fixture</h1><p>Only synthetic accounts are used here.</p>' +
      (unowned ? '<div id="login">' : '<form id="login" method="post" action="/submit?case=' + caseId + '">') +
      (!passwordOnly ? userField : '') + (!usernameOnly ? passwordField : '') +
      (phase === 'unowned-username' ? '<input type="password" name="hiddenPassword" hidden tabindex="-1" aria-hidden="true">' : '') +
      (phase === 'explicit-username' ? '<label>Unrelated text <input id="auxiliary" value="PUBLIC_OTHER_TEXT"></label>' : '') +
      '<button type="' + (unowned ? 'button' : 'submit') + '">' + (usernameOnly ? 'Continue' : 'Log in') + '</button>' + (unowned ? '</div>' : '</form>') +
      '<script>const caseId=' + JSON.stringify(caseId) + ',expectedUser=' + JSON.stringify(username) + ',expectedPassword=' + JSON.stringify(password) + ';let inputs=0,changes=0,submissions=0,commandId=0,telemetrySequence=0;' +
      'const report=()=>fetch("/telemetry?case="+caseId,{method:"POST",body:JSON.stringify({ready:true,inputs,changes,submissions,commandId,telemetrySequence:++telemetrySequence,timeOrigin:performance.timeOrigin,usernamePresent:!!document.querySelector("#j_username"),passwordPresent:!!document.querySelector("#j_password"),usernameAccepted:document.querySelector("#j_username")?.value===expectedUser,passwordAccepted:document.querySelector("#j_password")?.value===expectedPassword,unowned:!document.querySelector("#j_username,#j_password")?.form,decoyEmpty:!document.querySelector("[name=hiddenPassword]")?.value,auxiliaryUnchanged:document.querySelector("#auxiliary")?.value==="PUBLIC_OTHER_TEXT"})});' +
      'document.addEventListener("input",()=>{inputs++;report()});document.addEventListener("change",()=>{changes++;report()});document.addEventListener("submit",e=>{submissions++;e.preventDefault();report()});' +
      'setInterval(async()=>{const c=await(await fetch("/command?case="+caseId)).json();if(!c||c.id<=commandId)return;commandId=c.id;' +
      'if(c.action==="replace"){const f=document.querySelector("#login");f.replaceWith(f.cloneNode(true))}' +
      'else if(c.action==="navigate"){location.assign("/page?case="+encodeURIComponent(c.destination));return}' +
      'else if(c.action==="password-stage"){document.querySelector("#login").innerHTML=' + JSON.stringify(passwordField + '<button type="submit">Log in</button>') + '}' +
      'await report()},100);report();</script>');
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(18766, '127.0.0.1', resolve); });
}

if (process.platform !== 'win32') throw new Error('Native autofill regression requires Windows DPAPI and CEF.');
try {
  assert.equal(await alivePipe(), false, 'Random fixture pipe is already open');
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_AUTH_FIXTURE\n');
  seed = JSON.parse((await runFile(resolve(root, 'build/Release/vault_fixture_seed.exe'), [profile], { windowsHide: true })).stdout.trim());
  assert.equal(seed.origin, origin);
  const identities = Array.from({ length: 2 }, (_, index) => ({ clientId: 'autofill_client_' + index, token: randomBytes(32).toString('hex'), pipe }));
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: identities.map(identity => ({ id: identity.clientId, name: 'Synthetic autofill observer', tokenHash: createHash('sha256').update(identity.token).digest('hex') })),
    workspaces: [{ id: workspaceId, clients: identities.map(identity => identity.clientId) }], accountGrants: [], operations: [] }));
  for (let index = 0; index < identities.length; ++index) await writePrivateConfig(resolve(profile, 'client-' + index + '.json'), identities[index]);
  await startFixture(); applicationDllSha256 = await hashDll();
  browser = spawn(binary, ['--user-data-dir=' + profile, '--broker-pipe=' + pipeName, '--test-native-autofill'], { windowsHide: true, stdio: 'ignore' });
  let launchError; browser.once('error', error => { launchError = error; });
  await until(async () => { if (launchError) throw launchError; if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Owned browser exited before readiness'); return alivePipe(); }, 45000, 'Private autofill browser pipe');
  for (let index = 0; index < identities.length; ++index) {
    const client = new Client({ name: 'Synthetic native autofill observer', version: '1' }, { versionNegotiation: { mode: index ? 'legacy' : { pin: '2026-07-28' } } });
    await client.connect(new StdioClientTransport({ command: process.execPath, args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', resolve(profile, 'client-' + index + '.json')], stderr: 'pipe' }));
    clients.push(client); workers.push(await tool(index, 'worker_create', { name: 'Autofill observer ' + index, workspaceId }));
  }
  let filledTab, filledOffer;
  await check('Automatic native picker appears during cooldown after held input releases', async () => {
    const tab = await open('focus-timing');
    const before = await tool(0, 'control_status', scope(tab));
    const offered = await native('focus_offer', { tabId: tab.tabId });
    assert(offered.ok, 'Focus offer failed: ' + JSON.stringify(offered));
    assert.equal(offered.result.heldOfferSuppressed, true); assert.equal(offered.result.humanPaused, true);
    assert.equal(offered.result.pickerShown, true); assert(offered.result.offerElapsedMs < 1500);
    const paused = await tool(0, 'control_status', scope(tab));
    assert.equal(paused.ownerSessionId, before.ownerSessionId); assert.equal(paused.ownershipGeneration, before.ownershipGeneration);
    assert.equal(paused.humanPaused, true); empty('focus-timing');
    // Explicit requests also work during cooldown and replace the automatic offer.
    const explicit = await offer(tab); await native('dismiss', { offerId: explicit.offerId });
    await until(async () => !(await tool(0, 'control_status', scope(tab))).humanPaused);
    await tool(0, 'tab_close', mutation(tab));
    return { offerElapsedMs: offered.result.offerElapsedMs, pickerShown: true, heldInputSuppressed: true, agentPausePreserved: true, physicalMouseHookTested: false };
  });
  await check('Human autofill fills a CWL-shaped form without submission or agent account grants', async () => {
    filledTab = await open('cwl');
    const accounts = await tool(0, 'accounts', scope(filledTab)); assert.deepEqual(accounts.accounts, []);
    const before = await tool(0, 'control_status', scope(filledTab)), timeOrigin = states.get('cwl').timeOrigin;
    filledOffer = await offer(filledTab); empty('cwl'); await fill(filledOffer);
    await until(() => { const state = states.get('cwl'); return state.usernameAccepted && state.passwordAccepted && state.inputs === 2 && state.changes === 2; });
    await sleep(200); const state = states.get('cwl');
    assert.equal(state.submissions, 0); assert.equal(state.networkSubmissions, 0); assert.equal(state.timeOrigin, timeOrigin);
    assert.equal(state.inputs, 2); assert.equal(state.changes, 2);
    const after = await tool(0, 'control_status', scope(filledTab));
    assert.equal(after.ownerSessionId, before.ownerSessionId); assert.equal(after.ownershipGeneration, before.ownershipGeneration);
    assert.equal(after.protected, true); assert.equal(JSON.parse(await readFile(resolve(profile, 'broker-state.json'), 'utf8')).accountGrants.length, 0);
    return { nativeFill: true, accountGrants: 0, ownerPreserved: true, generationPreserved: true, submitted: false };
  });
  await check('Autofilled credentials remain protected from all clients through ownership handoff', async () => {
    await expectSealed(filledTab);
    const moved = await tool(0, 'control', { ...scope(filledTab), action: 'handoff', expectedGeneration: filledTab.ownershipGeneration, toSessionId: workers[1].agentSessionId });
    assert.equal(moved.protected, true);
    const committed = await until(async () => { const state = await tool(0, 'control_status', scope(filledTab)); return state.ownerSessionId === workers[1].agentSessionId && !state.handoffPending ? state : false; });
    assert(committed.ownershipGeneration > filledTab.ownershipGeneration);
    Object.assign(filledTab, workers[1], committed);
    await expectSealed(filledTab);
    const replay = await native('fill', { offerId: filledOffer.offerId, accountId: seed.accountId }); rejectedAs(replay, 'autofill_stale');
    assert.equal(states.get('cwl').inputs, 2); assert.equal(states.get('cwl').networkSubmissions, 0);
    return { observingClients: 2, modernAndLegacy: true, screenshotsWithheld: true, oneTimeOffer: true };
  });
  await check('Saved accounts cannot autofill a different HTTPS origin', async () => {
    const tab = await open('other-origin', 'credentials', otherOrigin), reply = await native('request', { tabId: tab.tabId });
    rejectedAs(reply, 'autofill_unavailable');
    await sleep(200); empty('other-origin'); return { exactOriginDenied: true, filled: false };
  });
  await check('Navigation invalidates an already offered native account selection', async () => {
    const tab = await open('stale-navigation'), offered = await offer(tab);
    await command('stale-navigation', 'navigate', { destination: 'navigation-destination' });
    await until(() => states.get('navigation-destination')?.ready);
    const rejected = await native('fill', { offerId: offered.offerId, accountId: seed.accountId }); rejectedAs(rejected, 'autofill_stale');
    await sleep(200); empty('stale-navigation'); empty('navigation-destination'); return { staleOfferDenied: true, newDocumentUntouched: true };
  });
  await check('Replacing the bound form cannot redirect an existing autofill offer', async () => {
    const tab = await open('changed-fields'), offered = await offer(tab);
    await command('changed-fields', 'replace');
    const rejected = await native('fill', { offerId: offered.offerId, accountId: seed.accountId }); rejectedAs(rejected, 'autofill_form_changed');
    await sleep(200); empty('changed-fields'); return { replacedNodesDenied: true, events: 0 };
  });
  await check('Dismissing a native offer prevents subsequent filling', async () => {
    const tab = await open('dismissed'), offered = await offer(tab);
    assert((await native('dismiss', { offerId: offered.offerId })).ok);
    const valid = await native('valid', { offerId: offered.offerId }); assert.equal(valid.result.valid, false);
    const rejected = await native('fill', { offerId: offered.offerId, accountId: seed.accountId }); rejectedAs(rejected, 'autofill_stale');
    await sleep(200); empty('dismissed'); return { dismissedOfferDenied: true, events: 0 };
  });
  await check('Username-first and password-only steps fill only the current phase without advancing', async () => {
    const tab = await open('username-first', 'username'), offered = await offer(tab); await fill(offered);
    await until(() => { const state = states.get('username-first'); return state.usernameAccepted && state.inputs === 1 && state.changes === 1; });
    let state = states.get('username-first'); assert.equal(state.passwordPresent, false); assert.equal(state.passwordAccepted, false); assert.equal(state.inputs, 1); assert.equal(state.changes, 1); assert.equal(state.submissions, 0); assert.equal(state.networkSubmissions, 0);
    await expectSealed(tab);
    await command('username-first', 'password-stage');
    const nextOffer = await offer(tab); await fill(nextOffer); await until(() => { const state = states.get('username-first'); return state.passwordAccepted && state.inputs === 2 && state.changes === 2; });
    state = states.get('username-first'); assert.equal(state.usernamePresent, false); assert.equal(state.inputs, 2); assert.equal(state.changes, 2); assert.equal(state.submissions, 0); assert.equal(state.networkSubmissions, 0);
    await expectSealed(tab); return { usernameOnlyFirst: true, passwordOnlySecond: true, nativeSelections: 2, submitted: false };
  });
  await check('Google-shaped unowned login phases and explicit usernames in richer forms fill only bound credentials', async () => {
    const details = [];
    for (const phase of ['unowned-username', 'unowned-password', 'explicit-username']) {
      const tab = await open(phase, phase);
      if (phase === 'unowned-username') {
        const automatic = await native('focus_offer', { tabId: tab.tabId }); assert(automatic.ok, 'Unowned focus offer failed: ' + JSON.stringify(automatic));
        assert.equal(automatic.result.pickerShown, true); assert.equal(automatic.result.heldOfferSuppressed, true);
        details.push({ phase, offerElapsedMs: automatic.result.offerElapsedMs, automaticPickerShown: true });
      }
      const offered = await offer(tab); await fill(offered);
      const expectedEvents = phase === 'explicit-username' ? 2 : 1;
      await until(() => { const state = states.get(phase); return state.inputs === expectedEvents && state.changes === expectedEvents &&
        (phase === 'unowned-password' ? state.passwordAccepted : state.usernameAccepted); });
      const state = states.get(phase); assert.equal(state.submissions, 0); assert.equal(state.networkSubmissions, 0);
      if (phase.startsWith('unowned-')) assert.equal(state.unowned, true);
      if (phase === 'unowned-username') { assert.equal(state.passwordPresent, false); assert.equal(state.decoyEmpty, true); }
      if (phase === 'explicit-username') { assert.equal(state.passwordAccepted, true); assert.equal(state.auxiliaryUnchanged, true); }
      await expectSealed(tab);
      const stale = await native('fill', { offerId: offered.offerId, accountId: seed.accountId }); rejectedAs(stale, 'autofill_stale');
    }
    // Node identity applies to synthetic groups just as it does to real forms.
    const tab = await open('unowned-replaced', 'unowned-username'), offered = await offer(tab);
    await command('unowned-replaced', 'replace');
    rejectedAs(await native('fill', { offerId: offered.offerId, accountId: seed.accountId }), 'autofill_form_changed');
    empty('unowned-replaced');
    return { phases: 3, submitted: false, hiddenDecoyUntouched: true, auxiliaryTextPreserved: true, unownedReplacementsDenied: true, realGoogleTested: false, details };
  });
  let cancelTab;
  await check('Locking the native vault invalidates pending offers and prevents new autofill', async () => {
    cancelTab = await open('cancel-pending');
    const tab = await open('locked'), offered = await offer(tab);
    assert.equal((await native('lock')).result.locked, true);
    const rejected = await native('fill', { offerId: offered.offerId, accountId: seed.accountId }); rejectedAs(rejected, 'autofill_stale');
    const request = await native('request', { tabId: tab.tabId }); rejectedAs(request, 'autofill_unavailable');
    await sleep(200); empty('locked'); return { vaultLocked: true, priorOfferDenied: true, newOfferDenied: true, events: 0 };
  });
  await check('Lock and unlock cannot revive an accepted fill waiting for a finite action to drain', async () => {
    assert.equal((await native('unlock')).result.locked, false);
    const rejected = await native('cancel_pending', { tabId: cancelTab.tabId, accountId: seed.accountId }); rejectedAs(rejected, 'autofill_interrupted');
    assert.equal(rejected.guardPendingAtCancel, true); assert(rejected.fixtureOfferId);
    await sleep(900); empty('cancel-pending');
    rejectedAs(await native('fill', { offerId: rejected.fixtureOfferId, accountId: seed.accountId }), 'autofill_stale');
    const fresh = await offer(cancelTab); assert.notEqual(fresh.offerId, rejected.fixtureOfferId);
    await native('dismiss', { offerId: fresh.offerId });
    return { lockedAndUnlockedBeforeDrain: true, terminalStatus: 'autofill_interrupted', credentialEvents: 0, staleOfferDenied: true, freshOfferAvailable: true };
  });
} catch (error) {
  if (!results.some(result => !result.passed)) results.push({ name: 'Human autofill harness', passed: false, error: error.message });
  console.log('FAIL human autofill harness: ' + error.message);
} finally {
  for (const client of clients) await client.close().catch(() => {});
  if (browser?.pid) {
    let terminationError;
    if (browser.exitCode === null && browser.signalCode === null) try { await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }); } catch (error) { terminationError = error; }
    try { await until(() => browser.exitCode !== null || browser.signalCode !== null, 10000, 'Owned process exit'); await until(async () => !(await alivePipe()), 10000, 'Private pipe cleanup'); }
    catch (error) { results.push({ name: 'Owned process cleanup', passed: false, error: [terminationError?.message, error.message].filter(Boolean).join('\n') }); }
  }
  if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
  try { applicationDllSha256AtEnd = await hashDll(); assert.equal(applicationDllSha256AtEnd, applicationDllSha256, 'Browser DLL changed during test'); logCanaryScan = await scanLogs(profile); }
  catch (error) { results.push({ name: 'Binary and native log verification', passed: false, error: error.message }); }
  const report = { run, capturedAt: new Date().toISOString(), binary: binaryRelative, profile, applicationDllSha256, applicationDllSha256AtEnd,
    nativeDriver: 'AuthTest-only marked profile; real native API, no MCP autofill endpoint', physicalPickerUiTested: false,
    logCanaryScan, passed: results.length === 11 && results.every(result => result.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true }); await writeFile(resolve(root, 'out/human-autofill-integration-results.json'), JSON.stringify(report, null, 2));
  if (lastNativeReply) await writeFile(resolve(profile, 'last-native-autofill-reply.json'), JSON.stringify(lastNativeReply, null, 2));
  await writeFile(resolve(profile, 'fixture-telemetry.json'), JSON.stringify(Object.fromEntries(states), null, 2));
  console.log('Report: ' + resolve(root, 'out/human-autofill-integration-results.json')); process.exitCode = report.passed ? 0 : 1;
}
