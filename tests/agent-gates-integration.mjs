// Production CEF + MCP with one generated profile: an agent's own clicks in
// its own tab cannot reach the native prompts that open another app or share
// the screen. The page reports what Chromium answered; a native message driver
// aimed only at this harness's process checks that no prompt window opened and
// keeps the browser window beyond every monitor. No screen is captured and no
// app is opened.
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
const run = `agent-gates-integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile = resolve(root, '.cache', run);
const pipeName = `xenon-test-${run}`, pipePath = `\\\\.\\pipe\\${pipeName}`;
const binary = resolve(root, 'build/app/Release/Xenon.exe');
const applicationDll = resolve(root, 'build/app/Release/Xenon.dll');
const configPath = resolve(profile, 'client-config.json');
const workspaceId = 'native-default';
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const runFile = promisify(execFile);
const hashDll = async () => createHash('sha256').update(await readFile(applicationDll)).digest('hex');
const results = [], reports = [];
let browser, client, server, base, applicationDllSha256, applicationDllSha256AtEnd;

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

// Acts only on this harness's own Xenon process, found by PID: `park` moves its
// shell beyond every monitor; otherwise it lists the captions of its prompt windows.
async function nativeWindows(park = false) {
  const script = `$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class XenonGateFixture {
  delegate bool EnumProc(IntPtr window, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int size);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int cx, int cy, uint flags);
  public static List<IntPtr> Windows(uint pid, string kind) { var found = new List<IntPtr>();
    EnumWindows((window,data)=>{uint owner;GetWindowThreadProcessId(window,out owner);var name=new StringBuilder(64);GetClassName(window,name,name.Capacity);
      if(owner==pid&&IsWindowVisible(window)&&name.ToString()==kind)found.Add(window);return true;},IntPtr.Zero); return found; }
  public static string Caption(IntPtr window) { var text = new StringBuilder(256); GetWindowText(window, text, text.Capacity); return text.ToString(); }
}
'@
${park ? `foreach ($w in [XenonGateFixture]::Windows(${browser.pid}, 'XenonBrowserShell')) { [void][XenonGateFixture]::SetWindowPos($w, [IntPtr]1, [System.Windows.Forms.SystemInformation]::VirtualScreen.Left - 2400, 120, 1200, 760, 0x0010) }`
  : `[XenonGateFixture]::Windows(${browser.pid}, 'XenonBrowserPanel') | ForEach-Object { [XenonGateFixture]::Caption($_) }`}`;
  const { stdout } = await runFile('powershell.exe', ['-NoProfile', '-NonInteractive', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')], { windowsHide: true });
  return stdout.split(/\r?\n/).filter(Boolean);
}

const fixture = `<!doctype html><meta charset="utf-8"><title>Xenon agent gate fixture</title>
<button id="app" style="font:20px sans-serif;padding:12px">Open app link</button>
<button id="share" style="font:20px sans-serif;padding:12px">Share screen</button>
<script>
const report=data=>fetch('/report',{method:'POST',body:JSON.stringify(data)});
document.getElementById('app').onclick=()=>{location.href='mailto:agent-gate@example.invalid';report({probe:'app'})};
document.getElementById('share').onclick=()=>navigator.mediaDevices.getDisplayMedia({video:true}).then(()=>report({probe:'share',name:'allowed'}),error=>report({probe:'share',name:error.name}));
</script>`;
function startFixture() {
  server = http.createServer((request, response) => {
    if (request.method === 'POST' && request.url === '/report') {
      let body = '';
      request.on('data', chunk => { body += chunk; if (body.length > 4096) request.destroy(); });
      request.on('end', () => { try { reports.push(JSON.parse(body)); } catch {} response.writeHead(204).end(); });
      return;
    }
    if (request.method === 'GET' && request.url === '/gates') { response.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' }).end(fixture); return; }
    response.writeHead(404).end();
  });
  return new Promise((ok, fail) => { server.once('error', fail); server.listen(0, '127.0.0.1', () => { base = `http://127.0.0.1:${server.address().port}`; ok(); }); });
}
async function agentClick(tab, name) {
  const status = await call('control_status', scope(tab));
  const observed = await until(async () => {
    try { const value = await call('observe', { ...scope(tab), maxNodes: 50 }); return !value.loading && value.nodes?.some(node => node.name === name && node.ref) && value; }
    catch { return false; }
  }, 20_000, `Agent observation of ${name}`);
  const target = observed.nodes.find(node => node.name === name && node.ref);
  const clicked = await call('interact', { ...scope(tab), ownershipGeneration: status.ownershipGeneration, operationId: randomUUID(), observationId: observed.observationId, action: 'click', elementRef: target.ref });
  assert.equal(clicked.status, 'dispatched');
}

if (process.platform !== 'win32') throw new Error('Agent gate integration requires Windows.');
if (await alivePipe()) throw new Error('Generated test pipe is already in use. Start a fresh run.');
try {
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_AGENT_GATE_FIXTURE\n');
  await writeFile(resolve(profile, 'ui-settings.json'), JSON.stringify({ version: 1, introductionCompleted: true, automaticUpdateChecks: false }));
  const identity = { clientId: `gates_${randomBytes(8).toString('hex')}`, token: randomBytes(32).toString('hex'), pipe: pipePath };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic gate client', tokenHash: createHash('sha256').update(identity.token).digest('hex') }],
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
  await nativeWindows(true);
  const transport = new StdioClientTransport({ command: process.execPath, args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath], stderr: 'pipe' });
  client = new Client({ name: 'Xenon agent gate regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(transport);
  const worker = await call('worker_create', { name: 'Agent gate observer', workspaceId });
  const tab = { ...worker, ...await call('tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: `${base}/gates` }) };

  await check("An agent's click on a link for another app opens no prompt", async () => {
    await agentClick(tab, 'Open app link');
    await until(() => reports.find(entry => entry.probe === 'app'), 10_000, 'App-link click');
    await sleep(1500);
    const prompts = await nativeWindows();
    assert.ok(!prompts.includes('Open link in another app'), `Prompt windows: ${prompts.join(' | ')}`);
    return { prompts };
  });

  await check("An agent's click cannot start screen sharing or show its prompt", async () => {
    await agentClick(tab, 'Share screen');
    const result = await until(() => reports.find(entry => entry.probe === 'share'), 10_000, 'Share result');
    assert.equal(result.name, 'NotAllowedError');
    const prompts = await nativeWindows();
    assert.ok(!prompts.includes('Website permission request'), `Prompt windows: ${prompts.join(' | ')}`);
    const status = await call('control_status', scope(tab));
    assert.equal(status.ownerSessionId, worker.agentSessionId, 'The agent keeps its tab');
    return { share: result.name, prompts };
  });
} finally {
  if (client) await client.close().catch(() => {});
  if (browser?.pid && browser.exitCode === null && browser.signalCode === null) {
    const close = `Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class XenonGateClose { delegate bool EnumProc(IntPtr w, IntPtr d);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr d);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool PostMessage(IntPtr w, uint m, IntPtr a, IntPtr b);
  public static void Close(uint pid) { EnumWindows((w,d)=>{uint p;GetWindowThreadProcessId(w,out p);var s=new StringBuilder(64);GetClassName(w,s,64);if(p==pid&&s.ToString()=="XenonBrowserShell")PostMessage(w,0x10,IntPtr.Zero,IntPtr.Zero);return true;},IntPtr.Zero); } }
'@
[XenonGateClose]::Close(${browser.pid})`;
    await runFile('powershell.exe', ['-NoProfile', '-NonInteractive', '-EncodedCommand', Buffer.from(close, 'utf16le').toString('base64')], { windowsHide: true }).catch(() => {});
    await until(() => browser.exitCode !== null || browser.signalCode !== null, 30_000, 'Normal browser exit').catch(() => {});
    // Cleanup targets only the process tree this harness launched, never a name.
    if (browser.exitCode === null && browser.signalCode === null) await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
  }
  server?.close();
  applicationDllSha256AtEnd = await hashDll().catch(() => undefined);
  const passed = results.length === 2 && results.every(result => result.passed) && applicationDllSha256AtEnd === applicationDllSha256;
  const output = { run, capturedAt: new Date().toISOString(), binary: 'build/app/Release/Xenon.exe', applicationDllSha256, applicationDllSha256AtEnd,
    coverage: 'Production CEF and MCP with a loopback fixture; agent clicks through MCP in an agent-created tab; native message driver lists prompt windows of this process only; no screen capture or app launch',
    results, passed };
  await mkdir(resolve(root, 'out'), { recursive: true });
  await writeFile(resolve(root, 'out', `${run}.json`), JSON.stringify(output, null, 2));
  console.log(`Report: out/${run}.json`);
  if (!passed) process.exitCode = 1;
}
