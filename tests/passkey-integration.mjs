// Production CEF + MCP with one generated profile. Every probe calls
// navigator.credentials.get with an already-aborted signal: Chromium answers
// AbortError when Xenon forwards the request and NotAllowedError when the gate
// refuses it, so no authenticator, Windows Hello prompt or credential is used.
// The human tab is loaded through the shell's address bar with a native message
// driver aimed only at the process this harness launched; it is not physical
// input, and no suite here covers a physical click inside an agent tab.
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
const run = `passkey-integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile = resolve(root, '.cache', run);
const pipeName = `xenon-test-${run}`, pipePath = `\\\\.\\pipe\\${pipeName}`;
const binary = resolve(root, 'build/app/Release/Xenon.exe');
const applicationDll = resolve(root, 'build/app/Release/Xenon.dll');
const configPath = resolve(profile, 'client-config.json');
const workspaceId = 'native-default';
// ShellId::Address in native/src/cef_shell.cpp.
const ADDRESS = 2004, WM_CLOSE = 0x0010, WM_SETTEXT = 0x000c, WM_KEYDOWN = 0x0100, VK_RETURN = 0x0d;
const REFUSAL = 'Xenon allows passkey requests only from the person using the browser.';
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const runFile = promisify(execFile);
const hashDll = async () => createHash('sha256').update(await readFile(applicationDll)).digest('hex');
const results = [], reports = [];
let browser, client, server, base, frameBase, applicationDllSha256, applicationDllSha256AtEnd;

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
    console.log(`PASS ${name} ${JSON.stringify(details)}`);
  } catch (error) {
    results.push({ name, passed: false, elapsedMs: Date.now() - start, error: error.message });
    console.log(`FAIL ${name}: ${error.message}`);
    throw error;
  }
}
async function call(name, args = {}) {
  const reply = await client.callTool({ name: `xenon_${name}`, arguments: args });
  if (reply.isError) throw new Error(`${name}: ${JSON.stringify(reply.structuredContent ?? reply.content)}`);
  return reply.structuredContent;
}
const scope = tab => ({ agentSessionId: tab.agentSessionId, workspaceId: tab.workspaceId, tabId: tab.tabId });

// Sends messages only to this harness's own Xenon shell window, found by PID.
async function shellMessages(steps) {
  const lines = steps.map(([target, message, wParam, text]) => {
    const window = target === 'address' ? `[XenonPasskeyFixture]::GetDlgItem($shell,${ADDRESS})` : '$shell';
    return text === undefined ? `[void][XenonPasskeyFixture]::PostMessage(${window},${message},[IntPtr]${wParam},[IntPtr]::Zero)`
      : `[void][XenonPasskeyFixture]::SendMessage(${window},${message},[IntPtr]::Zero,'${text}')`;
  });
  const script = `$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class XenonPasskeyFixture {
  delegate bool EnumProc(IntPtr window, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int size);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wp, string lp);
  public static IntPtr Shell(uint pid) {
    IntPtr result=IntPtr.Zero;
    EnumWindows((window,data)=>{uint owner;GetWindowThreadProcessId(window,out owner);var name=new StringBuilder(64);GetClassName(window,name,name.Capacity);
      if(owner==pid&&name.ToString()=="XenonBrowserShell"){result=window;return false;}return true;},IntPtr.Zero);
    return result;
  }
}
'@
$shell=[XenonPasskeyFixture]::Shell(${browser.pid})
if ($shell -eq [IntPtr]::Zero) { throw 'Fixture shell window not found' }
${lines.join('\n')}`;
  await runFile('powershell.exe', ['-NoProfile', '-NonInteractive', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')], { windowsHide: true });
}

function page(mode, kind) {
  const frames = kind === 'top' ? `<iframe src="${frameBase}/gate?mode=${mode}&kind=cross" allow="publickey-credentials-get *"></iframe>
<iframe src="/gate?mode=${mode}&kind=same"></iframe>` : '';
  return `<!doctype html><meta charset="utf-8"><title>Xenon passkey fixture ${mode} ${kind}</title>
${kind === 'top' ? '<button id="probe" style="font:20px sans-serif;padding:12px">Probe passkey</button>' : ''}${frames}
<script>
const mode=${JSON.stringify(mode)},kind=${JSON.stringify(kind)};
const report=(phase,data)=>fetch(location.origin+'/report',{method:'POST',body:JSON.stringify({mode,kind,phase,...data})});
const probe=async(phase,credentials=navigator.credentials)=>{
  let result;
  try{await credentials.get({publicKey:{challenge:new Uint8Array(32)},signal:AbortSignal.abort()});result={name:'resolved'}}
  catch(error){result={name:error.name,message:error.message}}
  report(phase,result);
};
report('wrapper',{wrapped:!/native code/.test(String(CredentialsContainer.prototype.get))});
probe('load');
if(kind==='top'){
  document.getElementById('probe').onclick=()=>probe('click');
  const blank=document.createElement('iframe');document.body.append(blank);probe('blank-frame',blank.contentWindow.navigator.credentials);
}
</script>`;
}
function startFixture() {
  const handler = (request, response) => {
    const url = new URL(request.url, 'http://localhost');
    if (request.method === 'POST' && url.pathname === '/report') {
      let body = '';
      request.on('data', chunk => { body += chunk; if (body.length > 4096) request.destroy(); });
      request.on('end', () => { try { reports.push(JSON.parse(body)); } catch {} response.writeHead(204).end(); });
      return;
    }
    const mode = url.searchParams.get('mode'), kind = url.searchParams.get('kind') ?? 'top';
    if (request.method === 'GET' && url.pathname === '/plain') { response.writeHead(200, { 'content-type': 'text/html' }).end('<!doctype html><title>Plain fixture</title><p>Plain</p>'); return; }
    if (request.method !== 'GET' || url.pathname !== '/gate' || !['human', 'agent'].includes(mode) || !['top', 'same', 'cross'].includes(kind)) { response.writeHead(404).end(); return; }
    response.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' }).end(page(mode, kind));
  };
  server = [http.createServer(handler), http.createServer(handler)];
  return new Promise((ok, fail) => {
    server[0].once('error', fail);
    server[0].listen(0, '127.0.0.1', () => {
      const { port } = server[0].address();
      // localhost and 127.0.0.1 are different sites, so the second frame is out of process.
      base = `http://localhost:${port}`; frameBase = `http://127.0.0.1:${port}`;
      server[1].once('error', () => ok());
      server[1].listen(port, '::1', ok);
    });
  });
}
const report = (mode, kind, phase) => reports.find(entry => entry.mode === mode && entry.kind === kind && entry.phase === phase);
const waitReports = (mode, phases, label) => until(() => phases.every(([kind, phase]) => report(mode, kind, phase)), 20_000, label);
const surfaces = [['top', 'wrapper'], ['top', 'load'], ['top', 'blank-frame'], ['same', 'wrapper'], ['same', 'load'], ['cross', 'wrapper'], ['cross', 'load']];
function expectAll(mode, name, refused) {
  const summary = {};
  for (const [kind, phase] of surfaces) {
    const entry = report(mode, kind, phase);
    if (phase === 'wrapper') { assert.equal(entry.wrapped, true, `${mode} ${kind} document must be wrapped before its own script runs`); continue; }
    assert.equal(entry.name, refused ? 'NotAllowedError' : 'AbortError', `${name}: ${kind} ${phase}`);
    if (refused) assert.equal(entry.message, REFUSAL);
    summary[`${kind}.${phase}`] = entry.name;
  }
  return summary;
}
async function observeAgent(tab, predicate, label) {
  return until(async () => {
    try { const value = await call('observe', { ...scope(tab), maxNodes: 50 }); return !value.loading && predicate(value) && value; }
    catch { return false; }
  }, 20_000, label);
}

if (process.platform !== 'win32') throw new Error('Passkey integration requires Windows.');
if (await alivePipe()) throw new Error('Generated test pipe is already in use. Start a fresh run.');
try {
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_PASSKEY_FIXTURE\n');
  await writeFile(resolve(profile, 'ui-settings.json'), JSON.stringify({ version: 1, introductionCompleted: true, automaticUpdateChecks: false }));
  const identity = { clientId: `passkey_${randomBytes(8).toString('hex')}`, token: randomBytes(32).toString('hex'), pipe: pipePath };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic passkey client', tokenHash: createHash('sha256').update(identity.token).digest('hex') }],
    workspaces: [{ id: workspaceId, clients: [identity.clientId] }], accountGrants: [], operations: [] }));
  await writePrivateConfig(configPath, identity);
  await startFixture();
  applicationDllSha256 = await hashDll();
  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { stdio: 'ignore' });
  let launchError; browser.once('error', error => { launchError = error; });
  await until(async () => {
    if (launchError) throw launchError;
    if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Browser exited before its private pipe became ready');
    return alivePipe();
  }, 45_000, 'Browser private pipe');
  const transport = new StdioClientTransport({ command: process.execPath, args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath], stderr: 'pipe' });
  client = new Client({ name: 'Xenon passkey regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(transport);
  const worker = await call('worker_create', { name: 'Passkey gate observer', workspaceId });

  let agentTab;
  await check('A tab created for an agent loads its website only after the passkey gate is installed in the page and every frame', async () => {
    agentTab = { ...worker, ...await call('tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: `${base}/gate?mode=agent` }) };
    await waitReports('agent', surfaces, 'Agent tab probes');
    const summary = expectAll('agent', 'Agent tab page script', true);
    const observed = await observeAgent(agentTab, value => value.nodes?.some(node => node.name === 'Probe passkey'), 'Agent tab observation');
    assert.equal(observed.canGoBack, false, 'The placeholder about:blank entry is not left in history');
    const status = await call('control_status', scope(agentTab));
    assert.equal(status.passkeyNeedsHuman, true, 'The agent is told that the human must start the passkey');
    assert.equal(status.ownerSessionId, worker.agentSessionId);
    return { ...summary, canGoBack: observed.canGoBack, passkeyNeedsHuman: status.passkeyNeedsHuman };
  });

  await check('An agent click on a passkey control is refused before reaching the authenticator', async () => {
    const before = await call('control_status', scope(agentTab));
    const observed = await observeAgent(agentTab, value => value.nodes?.some(node => node.name === 'Probe passkey' && node.ref), 'Agent probe control');
    const target = observed.nodes.find(node => node.name === 'Probe passkey' && node.ref);
    const clicked = await call('interact', { ...scope(agentTab), ownershipGeneration: before.ownershipGeneration, operationId: randomUUID(),
      observationId: observed.observationId, action: 'click', elementRef: target.ref });
    assert.equal(clicked.status, 'dispatched');
    const result = await until(() => report('agent', 'top', 'click'), 10_000, 'Agent click probe');
    assert.equal(result.name, 'NotAllowedError'); assert.equal(result.message, REFUSAL);
    const after = await call('control_status', scope(agentTab));
    assert.equal(after.ownerSessionId, before.ownerSessionId); assert.equal(after.ownershipGeneration, before.ownershipGeneration);
    assert.equal(after.humanPaused, false, 'A refusal does not pause the agent');
    return { click: result.name, ownershipGeneration: after.ownershipGeneration };
  });

  await check('Navigation clears the passkey notice', async () => {
    const status = await call('control_status', scope(agentTab));
    await call('navigate', { ...scope(agentTab), ownershipGeneration: status.ownershipGeneration, operationId: randomUUID(), action: 'navigate', url: `${base}/plain` });
    const cleared = await until(async () => { const value = await call('control_status', scope(agentTab)); return value.passkeyNeedsHuman === false && value; }, 15_000, 'Cleared passkey notice');
    return { passkeyNeedsHuman: cleared.passkeyNeedsHuman };
  });

  await check('A tab the human controls forwards passkey requests unchanged', async () => {
    await shellMessages([['address', WM_SETTEXT, 0, `${base}/gate?mode=human`], ['address', WM_KEYDOWN, VK_RETURN]]);
    await waitReports('human', surfaces, 'Human tab probes');
    return expectAll('human', 'Human tab page script', false);
  });
} finally {
  if (client) await client.close().catch(() => {});
  if (browser?.pid && browser.exitCode === null && browser.signalCode === null) {
    await shellMessages([['shell', WM_CLOSE, 0]]).catch(() => {});
    await until(() => browser.exitCode !== null || browser.signalCode !== null, 30_000, 'Normal browser exit').catch(() => {});
    // Cleanup targets only the process tree this harness launched, never a name.
    if (browser.exitCode === null && browser.signalCode === null) await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
  }
  for (const listener of server ?? []) listener.close();
  applicationDllSha256AtEnd = await hashDll().catch(() => undefined);
  const passed = results.length === 4 && results.every(result => result.passed) && applicationDllSha256AtEnd === applicationDllSha256;
  const output = { run, capturedAt: new Date().toISOString(), binary: 'build/app/Release/Xenon.exe', applicationDllSha256, applicationDllSha256AtEnd,
    coverage: 'Production CEF and MCP with loopback fixtures and already-aborted WebAuthn requests; native message driver for the human tab address bar, not physical input; no authenticator or Windows prompt is used',
    results, passed };
  await mkdir(resolve(root, 'out'), { recursive: true });
  await writeFile(resolve(root, 'out', `${run}.json`), JSON.stringify(output, null, 2));
  console.log(`Report: out/${run}.json`);
  if (!passed) process.exitCode = 1;
}
