// Production browser, disposable profile, synthetic loopback page only.
// Checks frame pacing of a covered agent tab: 1 fps while idle with Xenon's
// engine patch, full rate during and shortly after agent operations and while
// selected, and agent clicks that still pass the two-frame input guard.
// With the stock engine the pacing call is ignored, so the tab stays at full
// rate; the click and selection checks still run.
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
const run = `frame-pacing-integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile = resolve(root, '.cache', run), pipeName = `xenon-test-${run}`;
const pipe = `\\\\.\\pipe\\${pipeName}`, configPath = resolve(profile, 'client-config.json');
const browserDirectoryIndex = process.argv.indexOf('--browser-dir');
if (browserDirectoryIndex !== -1) assert(process.argv[browserDirectoryIndex + 1] && !process.argv[browserDirectoryIndex + 1].startsWith('--'), '--browser-dir requires a complete production browser directory');
const binary = resolve(root, browserDirectoryIndex === -1 ? 'build/app/Release' : process.argv[browserDirectoryIndex + 1], 'Xenon.exe');
const runFile = promisify(execFile), sleep = ms => new Promise(r => setTimeout(r, ms));
const hashes = async () => Object.fromEntries(await Promise.all(['Xenon.dll', 'libcef.dll'].map(async name =>
  [name, createHash('sha256').update(await readFile(resolve(binary, '..', name))).digest('hex')])));
// Idle covered tabs run at 1 fps and agent leases last 15 seconds
// (native/include/xenon/tab_render_pacing.hpp).
const leaseMs = 15_000, settleMs = 3_000, idleMax = 3, fullMin = 20;
const fixture = `<!doctype html><meta charset="utf-8"><title>Xenon frame pacing fixture</title>
<h1>Frame pacing</h1><button id="target">Pacing target</button>
<script>
let frames=0;const tick=()=>{frames++;requestAnimationFrame(tick)};requestAnimationFrame(tick);
setInterval(()=>{const value=frames;frames=0;fetch('/rate',{method:'POST',body:String(value)})},1000);
document.querySelector('#target').addEventListener('click',()=>fetch('/clicked',{method:'POST'}));
</script>`;
let browser, client, server, startHashes, endHashes, failure, pacingSupported = false;
const samples = [], checks = [];
let clicked = 0;
const alivePipe = () => new Promise(done => {
  const socket = net.connect(pipe);
  socket.once('connect', () => { socket.destroy(); done(true); });
  socket.once('error', () => { socket.destroy(); done(false); });
  socket.setTimeout(1500, () => { socket.destroy(); done(false); });
});
async function until(fn, label, ms = 30_000) {
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) { const value = await fn(); if (value) return value; await sleep(150); }
  throw new Error(`${label} timed out`);
}
async function tool(name, args) {
  const value = await client.callTool({ name: `xenon_${name}`, arguments: args });
  assert(!value.isError, `${name}: ${JSON.stringify(value.structuredContent)}`);
  return value.structuredContent;
}
// Animation frames per second reported by the page during the next window.
async function measure(label, expectFull) {
  const from = Date.now(); await sleep(settleMs);
  const rates = samples.filter(sample => sample.at > from + 1000).map(sample => sample.frames);
  assert(rates.length >= 1, `${label}: the page reported no frame rate`);
  const result = { label, rates, expected: expectFull ? 'full' : 'idle' };
  checks.push(result); console.log(JSON.stringify(result));
  if (expectFull) assert(Math.min(...rates) >= fullMin, `${label}: expected full frame rate, got ${rates}`);
  else assert(Math.max(...rates) <= idleMax, `${label}: expected the idle frame rate, got ${rates}`);
}
const select = index => runFile('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
  resolve(root, 'tests/fixtures/select-shell-tab.ps1'), '-ProcessId', String(browser.pid), '-Index', String(index)], { windowsHide: true });

try {
  assert.equal(process.platform, 'win32');
  await mkdir(profile, { recursive: true });
  const identity = { clientId: `pacing_${randomBytes(8).toString('hex')}`, token: randomBytes(32).toString('hex'), pipe };
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_PACING_FIXTURE\n');
  await writeFile(resolve(profile, 'ui-settings.json'), JSON.stringify({ version: 1, introductionCompleted: true }));
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic pacing client', tokenHash: createHash('sha256').update(identity.token).digest('hex') }],
    workspaces: [{ id: 'native-default', clients: [identity.clientId] }], accountGrants: [], operations: [] }));
  await writePrivateConfig(configPath, identity);
  server = http.createServer((request, response) => {
    if (request.method === 'POST' && ['/rate', '/clicked'].includes(request.url)) {
      let data = '';
      request.on('data', chunk => { data += chunk; if (data.length > 64) request.destroy(); });
      request.on('end', () => {
        if (request.url === '/clicked') clicked++;
        else if (/^\d+$/.test(data)) samples.push({ at: Date.now(), frames: Number(data) });
        response.writeHead(204).end();
      });
    } else if (request.method === 'GET' && request.url === '/') {
      response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' }).end(fixture);
    } else response.writeHead(404).end();
  });
  await new Promise(done => server.listen(0, '127.0.0.1', done));
  startHashes = await hashes();
  // Only Xenon's audited engine builds (engine or codec recipe) carry the pacing patch.
  const lock = JSON.parse(await readFile(resolve(root, 'dependencies.lock.json'), 'utf8'));
  const provenance = async (name, recipe) => ({ build: await readFile(resolve(binary, '..', name), 'utf8').then(JSON.parse, () => null), recipe });
  const engine = [await provenance('xenon-engine-build.json', lock.cef.engineSourceBuild),
    await provenance('xenon-codec-build.json', lock.cef.mediaSourceBuild)].find(candidate => candidate.build);
  pacingSupported = !!engine && engine.build.recipe?.backgroundFrameRatePatch?.sha256 === engine.recipe.backgroundFrameRatePatch.sha256 &&
    engine.build.files?.['Release/libcef.dll'] === startHashes['libcef.dll'];
  console.log(`Engine frame pacing: ${pacingSupported ? 'patched engine' : 'stock engine, pacing ignored'}`);
  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { stdio: 'ignore' });
  let launchError; browser.once('error', error => { launchError = error; });
  await until(() => {
    if (launchError) throw launchError;
    if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Browser exited before its private pipe became ready');
    return alivePipe();
  }, 'Browser startup', 45_000);
  client = new Client({ name: 'Xenon frame pacing regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(new StdioClientTransport({ command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath], stderr: 'pipe' }));
  const worker = await tool('worker_create', { name: 'Pacing observer', workspaceId: 'native-default' });
  // Agent tabs open beneath the selected blank tab, so this tab is covered.
  const tab = { ...worker, ...await tool('tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: `http://127.0.0.1:${server.address().port}/` }) };
  const scope = { agentSessionId: tab.agentSessionId, workspaceId: tab.workspaceId, tabId: tab.tabId };
  await until(() => samples.length > 0, 'Frame rate reports');
  await sleep(leaseMs);
  await measure('Covered tab after its creation lease', !pacingSupported);
  const observation = await until(async () => {
    const response = await client.callTool({ name: 'xenon_observe', arguments: { ...scope, maxNodes: 100 } });
    if (response.isError) {
      if (['privacy_guard_initializing', 'page_changed'].includes(response.structuredContent?.error?.code)) return false;
      throw Error(JSON.stringify(response.structuredContent));
    }
    return response.structuredContent.nodes?.some(n => n.role === 'button' && n.name === 'Pacing target' && n.ref) ? response.structuredContent : false;
  }, 'Pacing target');
  await measure('Covered tab after an agent observation', true);
  await sleep(leaseMs);
  await measure('Covered tab after the observation lease', !pacingSupported);
  // The click path itself must restore full rate before the two-frame guard.
  const button = observation.nodes.find(n => n.role === 'button' && n.name === 'Pacing target' && n.ref);
  const clickedAt = Date.now();
  await tool('interact', { ...scope, ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(),
    observationId: observation.observationId, action: 'click', elementRef: button.ref });
  await until(() => clicked === 1, 'Page click');
  checks.push({ label: 'Click on an idle covered tab', milliseconds: Date.now() - clickedAt });
  await sleep(leaseMs);
  await measure('Covered tab after the click lease', !pacingSupported);
  // Selection alone restores full rate, and covering the tab again restores
  // the idle rate without waiting for a lease.
  await select(1);
  await measure('Selected tab without agent activity', true);
  await select(0);
  await measure('Tab covered again after selection', !pacingSupported);
} catch (error) { failure = error.message; console.error(error); process.exitCode = 1; }
finally {
  await client?.close().catch(() => {});
  if (browser?.pid && browser.exitCode === null && browser.signalCode === null)
    await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
  server?.close();
  endHashes = await hashes().catch(() => undefined);
  if (JSON.stringify(startHashes) !== JSON.stringify(endHashes)) { failure = 'Browser binaries changed during the test'; process.exitCode = 1; }
  await mkdir(resolve(root, 'out'), { recursive: true });
  await writeFile(resolve(root, 'out', `${run}.json`), JSON.stringify({ run, startHashes, endHashes, pacingSupported, checks,
    coverage: 'Synthetic loopback page counting requestAnimationFrame callbacks in an agent tab covered by the selected blank tab: idle, after observe and click leases, while selected through the shell tree, and covered again. Agent click on an idle covered tab. No GPU presentation timing, website or physical input.',
    failure, passed: !failure }, null, 2));
  console.log(`Report: out/${run}.json`);
}
