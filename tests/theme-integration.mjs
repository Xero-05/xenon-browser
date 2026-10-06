// Production CEF + MCP with one generated profile and four normal launches.
// Website evidence is the loopback fixture's own matchMedia report plus page
// screenshots. Theme menu commands use a native message driver aimed only at
// the process this harness launched; it is not physical input. Windows app mode
// is read, never changed: run once in each Windows mode to cover both.
// No existing browser profile, raw CDP, credential or real website is accessed.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { inflateSync } from 'node:zlib';
import { createHash, randomBytes } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';

const root = resolve(import.meta.dirname, '..');
const run = `theme-integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile = resolve(root, '.cache', run);
const pipeName = `xenon-test-${run}`, pipePath = `\\\\.\\pipe\\${pipeName}`;
const binary = resolve(root, 'build/app/Release/Xenon.exe');
const applicationDll = resolve(root, 'build/app/Release/Xenon.dll');
const configPath = resolve(profile, 'client-config.json');
const settingsPath = resolve(profile, 'ui-settings.json');
const workspaceId = 'native-default';
// ShellId values in native/src/cef_shell.cpp.
const themeCommands = { system: 2111, light: 2112, dark: 2113 };
const WM_CLOSE = 0x0010, WM_COMMAND = 0x0111;
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const runFile = promisify(execFile);
const hashDll = async () => createHash('sha256').update(await readFile(applicationDll)).digest('hex');
const results = [], launches = [], reports = new Map();
let browser, client, server, base, windowsScheme, applicationDllSha256, applicationDllSha256AtEnd;

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
  return reply;
}
const tool = async (name, args) => (await call(name, args)).structuredContent;
const workerScope = worker => ({ agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId });
const scope = tab => ({ ...workerScope(tab), tabId: tab.tabId });

async function readWindowsScheme() {
  try {
    const { stdout } = await runFile('reg.exe', ['query', 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize',
      '/v', 'AppsUseLightTheme'], { windowsHide: true });
    const match = /AppsUseLightTheme\s+REG_DWORD\s+0x([0-9a-f]+)/i.exec(stdout);
    return match && Number.parseInt(match[1], 16) === 0 ? 'dark' : 'light';
  } catch { return 'light'; } // A missing value means light, as in Xenon's palette.
}
// Posts one message to this harness's own Xenon shell window, found by PID.
async function postToShell(message, wParam = 0) {
  const script = `$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class XenonThemeFixture {
  delegate bool EnumProc(IntPtr window, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int size);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
  public static IntPtr Shell(uint pid) {
    IntPtr result=IntPtr.Zero;
    EnumWindows((window,data)=>{uint owner;GetWindowThreadProcessId(window,out owner);var name=new StringBuilder(64);GetClassName(window,name,name.Capacity);
      if(owner==pid&&name.ToString()=="XenonBrowserShell"){result=window;return false;}return true;},IntPtr.Zero);
    return result;
  }
}
'@
$shell=[XenonThemeFixture]::Shell(${browser.pid})
if ($shell -eq [IntPtr]::Zero) { throw 'Fixture shell window not found' }
if (-not [XenonThemeFixture]::PostMessage($shell,${message},[IntPtr]${wParam},[IntPtr]::Zero)) { throw 'Fixture message failed' }`;
  await runFile('powershell.exe', ['-NoProfile', '-NonInteractive', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')],
    { windowsHide: true });
}

// Minimal decoder for the 8-bit, non-interlaced RGB(A) PNGs CEF captures.
function pngPixel(buffer, fx, fy) {
  assert.equal(buffer.subarray(0, 8).toString('hex'), '89504e470d0a1a0a', 'Screenshot must be a PNG');
  let offset = 8, width, height, depth, type, interlace; const chunks = [];
  while (offset < buffer.length) {
    const length = buffer.readUInt32BE(offset), kind = buffer.toString('ascii', offset + 4, offset + 8);
    const body = buffer.subarray(offset + 8, offset + 8 + length);
    if (kind === 'IHDR') { width = body.readUInt32BE(0); height = body.readUInt32BE(4); depth = body[8]; type = body[9]; interlace = body[12]; }
    else if (kind === 'IDAT') chunks.push(body);
    else if (kind === 'IEND') break;
    offset += 12 + length;
  }
  assert(depth === 8 && (type === 2 || type === 6) && interlace === 0, 'Unsupported screenshot PNG layout');
  const channels = type === 6 ? 4 : 3, stride = width * channels, raw = inflateSync(Buffer.concat(chunks));
  const pixels = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)], line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    assert(filter <= 4, 'Invalid PNG filter');
    const out = pixels.subarray(y * stride, (y + 1) * stride), prior = y ? pixels.subarray((y - 1) * stride, y * stride) : undefined;
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? out[x - channels] : 0, b = prior ? prior[x] : 0, c = prior && x >= channels ? prior[x - channels] : 0;
      const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
      const predictor = [0, a, b, (a + b) >> 1, pa <= pb && pa <= pc ? a : pb <= pc ? b : c][filter];
      out[x] = (line[x] + predictor) & 255;
    }
  }
  const index = Math.floor(height * fy) * stride + Math.floor(width * fx) * channels;
  return [pixels[index], pixels[index + 1], pixels[index + 2]];
}
// Reads a blank area of the rendered page; both fixtures leave it unpainted.
async function paintedScheme(tab) {
  const reply = await call('screenshot', scope(tab));
  const image = reply.content.find(entry => entry.type === 'image');
  assert(image, 'Screenshot must include image bytes');
  const rgb = pngPixel(Buffer.from(image.data, 'base64'), 0.8, 0.8);
  const luminance = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
  assert(luminance < 80 || luminance > 175, `Page background ${rgb} is neither light nor dark`);
  return { scheme: luminance < 80 ? 'dark' : 'light', rgb };
}
async function loaded(tab) {
  return until(async () => {
    try { const value = await tool('observe', { ...scope(tab), maxNodes: 50 }); return !value.loading && value; }
    catch { return false; }
  }, 20_000, 'Page load');
}
async function openFixture(worker, page) {
  const url = new URL('/page', base); url.searchParams.set('page', page);
  const tab = { ...worker, ...await tool('tab_create', { ...workerScope(worker), url: url.href }) };
  const reported = await until(() => reports.get(page)?.[0], 20_000, `Fixture ${page} report`);
  const observed = await until(async () => {
    const value = await loaded(tab);
    return value.nodes?.some(node => node.name === `Preferred scheme: ${reported}`) && value;
  }, 20_000, `Fixture ${page} visible scheme`);
  return { tab, reported, painted: await paintedScheme(tab), observedNodes: observed.nodes.length };
}
async function openBlank(worker) {
  const tab = { ...worker, ...await tool('tab_create', workerScope(worker)) };
  await loaded(tab);
  return paintedScheme(tab);
}

function startFixture() {
  server = http.createServer((request, response) => {
    const url = new URL(request.url, 'http://127.0.0.1');
    const page = url.searchParams.get('page') ?? '';
    if (!/^[a-z0-9-]{1,64}$/.test(page)) { response.writeHead(400).end(); return; }
    if (request.method === 'POST' && url.pathname === '/report') {
      const scheme = url.searchParams.get('scheme');
      if (scheme === 'dark' || scheme === 'light') reports.set(page, [...(reports.get(page) ?? []), scheme]);
      response.writeHead(204).end(); return;
    }
    if (request.method !== 'GET' || url.pathname !== '/page') { response.writeHead(404).end(); return; }
    response.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
    // color-scheme lets the UA canvas follow the preferred scheme, so the page
    // screenshot checks rendering as well as the media query.
    response.end(`<!doctype html><meta charset="utf-8"><meta name="color-scheme" content="light dark">
<title>Xenon theme fixture ${page}</title><p id="scheme" style="font:20px sans-serif">Preferred scheme: pending</p>
<script>
const query=matchMedia('(prefers-color-scheme: dark)');
const report=()=>{const scheme=query.matches?'dark':'light';document.querySelector('#scheme').textContent='Preferred scheme: '+scheme;
  fetch('/report?page=${page}&scheme='+scheme,{method:'POST'});};
query.addEventListener('change',report);report();
</script>`);
  });
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => { base = `http://127.0.0.1:${server.address().port}`; resolve(); });
  });
}
async function writeTheme(theme) {
  await writeFile(settingsPath, JSON.stringify({ version: 1, theme, introductionCompleted: true }));
}
async function launch(theme) {
  assert.equal(await hashDll(), applicationDllSha256, 'Application changed between launches');
  assert.equal(JSON.parse(await readFile(settingsPath, 'utf8')).theme, theme, 'Launch must start from the expected saved theme');
  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { stdio: 'ignore' });
  let launchError;
  browser.once('error', error => { launchError = error; });
  launches.push({ pid: browser.pid, theme, startedAt: new Date().toISOString() });
  await until(async () => {
    if (launchError) throw launchError;
    if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Browser exited before its private pipe became ready');
    return alivePipe();
  }, 45_000, 'Browser private pipe');
  const transport = new StdioClientTransport({ command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath], stderr: 'pipe' });
  client = new Client({ name: 'Xenon theme regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(transport);
  return tool('worker_create', { name: `Theme ${theme} observer`, workspaceId });
}
async function exitNormally() {
  const target = browser;
  await client.close(); client = undefined;
  await postToShell(WM_CLOSE);
  await until(() => target.exitCode !== null || target.signalCode !== null, 30_000, 'Normal browser exit');
  assert.equal(target.exitCode, 0, 'Browser must exit normally');
  browser = undefined;
  await until(async () => !(await alivePipe()), 15_000, 'Pipe cleanup');
}
async function verifyLaunch(theme, expected, label) {
  const worker = await launch(theme);
  const fixture = await openFixture(worker, label);
  const blank = await openBlank(worker);
  assert.equal(fixture.reported, expected, `${theme} page prefers-color-scheme`);
  assert.equal(fixture.painted.scheme, expected, `${theme} page canvas`);
  assert.equal(blank.scheme, expected, `${theme} about:blank background`);
  return { worker, fixture, blank };
}

if (process.platform !== 'win32') throw new Error('Theme integration requires Windows.');
if (await alivePipe()) throw new Error('Generated test pipe is already in use. Start a fresh run.');
const opposite = scheme => scheme === 'dark' ? 'light' : 'dark';
try {
  windowsScheme = await readWindowsScheme();
  console.log(`Windows app mode: ${windowsScheme}`);
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_THEME_FIXTURE\n');
  const identity = { clientId: `theme_${randomBytes(8).toString('hex')}`, token: randomBytes(32).toString('hex'), pipe: pipePath };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic theme client', tokenHash: createHash('sha256').update(identity.token).digest('hex') }],
    workspaces: [{ id: workspaceId, clients: [identity.clientId] }], accountGrants: [], operations: [] }));
  await writePrivateConfig(configPath, identity);
  await startFixture();
  applicationDllSha256 = await hashDll();

  for (const theme of ['light', 'dark']) {
    await check(`Theme ${theme} forces ${theme} web content under Windows ${windowsScheme} mode`, async () => {
      await writeTheme(theme);
      const { fixture, blank } = await verifyLaunch(theme, theme, `${theme}-launch`);
      await exitNormally();
      return { reported: fixture.reported, pageRgb: fixture.painted.rgb, blankRgb: blank.rgb };
    });
  }
  const chosen = opposite(windowsScheme);
  await check(`Theme system follows Windows ${windowsScheme} mode; a runtime switch is saved for the next launch`, async () => {
    await writeTheme('system');
    const { worker, fixture, blank } = await verifyLaunch('system', windowsScheme, 'system-launch');
    await postToShell(WM_COMMAND, themeCommands[chosen]);
    await until(async () => JSON.parse(await readFile(settingsPath, 'utf8')).theme === chosen, 10_000, 'Saved runtime theme');
    // Chromium reads its forced color mode once per process, so pages keep the
    // startup scheme. The shell reports that a restart applies the change.
    const after = await openFixture(worker, 'system-after-switch');
    assert.equal(after.reported, windowsScheme, 'Pages opened after a runtime switch keep this run\'s scheme');
    assert.deepEqual(reports.get('system-launch'), [windowsScheme], 'An open page receives no scheme change before restart');
    await exitNormally();
    return { reported: fixture.reported, blankRgb: blank.rgb, savedTheme: chosen, afterSwitch: after.reported };
  });
  await check(`Restart applies the runtime ${chosen} selection to web content`, async () => {
    const { fixture, blank } = await verifyLaunch(chosen, chosen, 'restart-launch');
    await exitNormally();
    return { reported: fixture.reported, pageRgb: fixture.painted.rgb, blankRgb: blank.rgb };
  });
} finally {
  if (client) await client.close().catch(() => {});
  if (browser?.pid && browser.exitCode === null && browser.signalCode === null) {
    // Cleanup targets only the process tree this harness launched, never a name.
    await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
  }
  server?.close();
  applicationDllSha256AtEnd = await hashDll().catch(() => undefined);
  const passed = results.length === 4 && results.every(result => result.passed) && applicationDllSha256AtEnd === applicationDllSha256;
  const report = { run, capturedAt: new Date().toISOString(), binary: 'build/app/Release/Xenon.exe', applicationDllSha256, applicationDllSha256AtEnd,
    windowsAppMode: windowsScheme, coverage: 'Production CEF and MCP with a loopback fixture; native message driver for theme menu and close, not physical input',
    launches, results, passed };
  await mkdir(resolve(root, 'out'), { recursive: true });
  await writeFile(resolve(root, 'out', `${run}.json`), JSON.stringify(report, null, 2));
  console.log(`Report: out/${run}.json`);
  if (!passed) process.exitCode = 1;
}
