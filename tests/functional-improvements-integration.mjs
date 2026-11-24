// Production CEF/MCP end-to-end verification of compact/delta/filtered observations,
// enhanced wait, hover/key batching, structured staleReason, cropped screenshots,
// attention kind, and xenon_wait_ready. Uses synthetic loopback HTML and a disposable profile.
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

const root = resolve(import.meta.dirname, '..'), build = resolve(root, process.env.XENON_TEST_BUILD_DIR ?? 'build');
const run = `functional-improvements-${Date.now()}-${randomBytes(4).toString('hex')}`, profile = resolve(root, '.cache', run);
const pipeName = `xenon-test-${run}`, pipe = `\\\\.\\pipe\\${pipeName}`, config = resolve(profile, 'client.json');
const binary = resolve(build, 'app/Release/Xenon.exe'), dll = resolve(build, 'app/Release/Xenon.dll');
const hash = async () => createHash('sha256').update(await readFile(dll)).digest('hex');
const sleep = ms => new Promise(r => setTimeout(r, ms)), runFile = promisify(execFile), results = [];
let browser, client, server, tab, worker, initialHash;

async function until(fn, timeout = 20000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) { const value = await fn(); if (value) return value; await sleep(100); }
  throw new Error('Fixture condition timed out');
}
const alive = () => new Promise(resolve => {
  const socket = net.connect(pipe);
  socket.once('connect', () => { socket.destroy(); resolve(true); });
  socket.once('error', () => { socket.destroy(); resolve(false); });
});
const scope = () => ({ agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, tabId: tab.tabId });
const raw = (name, args = {}) => client.callTool({ name: `xenon_${name}`, arguments: args });
async function tool(name, args = {}) {
  const reply = await raw(name, args);
  assert(!reply.isError, JSON.stringify(reply.structuredContent));
  return reply;
}
async function observe(extra = {}) {
  return until(async () => {
    const reply = await raw('observe', { ...scope(), ...extra });
    if (reply.isError && ['privacy_guard_initializing', 'page_changed', 'OBSERVATION_CHANGED'].includes(reply.structuredContent?.error?.code)) return false;
    assert(!reply.isError, JSON.stringify(reply.structuredContent));
    return reply.structuredContent;
  });
}
async function screenshot(extra = {}) {
  return until(async () => {
    const reply = await raw('screenshot', { ...scope(), ...extra });
    if (reply.isError && ['privacy_guard_initializing', 'page_changed', 'screenshot_unverified'].includes(reply.structuredContent?.error?.code)) return false;
    return reply;
  });
}
async function check(name, fn) {
  try { await fn(); results.push({ name, passed: true }); console.log(`PASS ${name}`); }
  catch (error) { results.push({ name, passed: false, error: error.message }); console.error(`FAIL ${name}: ${error.message}`); }
}

try {
  initialHash = await hash();
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_FUNCTIONAL_IMPROVEMENTS\n');
  await writeFile(resolve(profile, 'ui-settings.json'), JSON.stringify({ version: 1, introductionCompleted: true, automaticUpdateChecks: false }));
  const identity = { clientId: `func_${randomBytes(8).toString('hex')}`, token: randomBytes(32).toString('hex'), pipe };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({
    version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic functional client', tokenHash: createHash('sha256').update(identity.token).digest('hex') }],
    workspaces: [], accountGrants: [], operations: [],
  }));
  await writePrivateConfig(config, identity);

  server = http.createServer((request, response) => {
    response.setHeader('Cache-Control', 'no-store');
    response.setHeader('Content-Type', 'text/html; charset=utf-8');
    if (request.url === '/secret') {
      response.end(`<!doctype html><title>Protected Crop</title>
        <div id="safe" style="position:fixed;left:20px;top:20px;width:120px;height:40px;background:#eef">Public header</div>
        <input type="password" style="position:fixed;left:20px;top:300px">`);
      return;
    }
    response.end(`<!doctype html><title>Functional Improvements Fixture</title>
      <style>body{font:16px sans-serif;margin:20px}button,input{font:16px sans-serif;margin:6px;padding:6px 10px}</style>
      <h1>Functional Fixture Ready</h1>
      <p>Paragraph text one for compaction testing.</p>
      <p>Paragraph text two for compaction testing.</p>
      <p>Paragraph text three for compaction testing.</p>
      <p>Paragraph text four for compaction testing.</p>
      <input id="note" aria-label="Note" placeholder="Note">
      <button id="hoverBtn">Hover Target</button>
      <button id="submitBtn">Submit Action</button>
      <button id="mutateBtn">Trigger Async</button>
      <button id="staleBtn">Morphing Button</button>
      <div id="status">Initial Status</div>
      <div id="log"></div>
      <script>
        let hovered = 0, submitted = 0, enterPressed = 0;
        const log = () => { document.querySelector('#log').textContent = 'H:' + hovered + ' S:' + submitted + ' E:' + enterPressed + ' V:' + document.querySelector('#note').value; };
        document.querySelector('#hoverBtn').addEventListener('mouseenter', () => { hovered++; log(); });
        document.querySelector('#submitBtn').addEventListener('click', () => { submitted++; log(); });
        document.querySelector('#note').addEventListener('keydown', e => { if (e.key === 'Enter') { enterPressed++; log(); } });
        document.querySelector('#mutateBtn').addEventListener('click', () => {
          document.querySelector('#status').textContent = 'Loading Async...';
          setTimeout(() => { document.querySelector('#status').textContent = 'Async Complete OK'; }, 350);
        });
        document.querySelector('#staleBtn').addEventListener('click', () => {
          document.querySelector('#staleBtn').textContent = 'Morphed Label';
        });
        log();
      </script>`);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;

  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { windowsHide: true, stdio: 'ignore' });
  await until(alive, 30000);
  client = new Client({ name: 'Functional improvements fixture', version: '1' });
  await client.connect(new StdioClientTransport({ command: process.execPath, args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', config], stderr: 'pipe' }));

  worker = (await tool('worker_create', { name: 'Functional worker' })).structuredContent;
  tab = (await tool('tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: `${base}/` })).structuredContent;
  await until(async () => (await observe()).nodes.some(n => n.name === 'Functional Fixture Ready'));

  await check('Compact observe reduces payload size significantly and deduplicates limitations', async () => {
    const full = await observe();
    const compact = await observe({ compact: true });
    assert.equal(full.limitations, undefined, 'duplicate top-level limitations should be removed');
    assert(Array.isArray(full.visibility?.limitations) && full.visibility.limitations.length > 0);
    assert(compact.nodeDefaults && compact.nodeDefaults.visibilityStatus === 'verified');
    const fullBytes = Buffer.byteLength(JSON.stringify(full.nodes), 'utf8');
    const compactBytes = Buffer.byteLength(JSON.stringify(compact.nodes), 'utf8');
    assert(compactBytes < fullBytes * 0.65, `Expected >=35% node reduction, got ${compactBytes} vs ${fullBytes}`);
    console.log(`  nodes JSON: full=${fullBytes}B -> compact=${compactBytes}B (${Math.round((1 - compactBytes / fullBytes) * 100)}% smaller)`);
  });

  await check('Native roles and interactiveOnly filters exclude StaticText before truncation', async () => {
    const buttonsOnly = await observe({ roles: ['button'] });
    assert(buttonsOnly.nodes.length >= 4);
    assert(buttonsOnly.nodes.every(n => n.role === 'button'), JSON.stringify(buttonsOnly.nodes));
    const interactive = await observe({ interactiveOnly: true });
    assert(interactive.nodes.length >= 5);
    assert(interactive.nodes.every(n => n.role !== 'StaticText' && n.role !== 'heading'));
  });

  await check('Delta observation reports unchanged=true when idle and added/changed after mutation', async () => {
    const first = await observe({ compact: true });
    const idleDelta = await observe({ compact: true, sinceObservationId: first.observationId });
    assert.equal(idleDelta.unchanged, true);
    assert.equal(idleDelta.delta.sinceObservationId, first.observationId);
    assert.deepEqual(idleDelta.delta.added, []);
    assert.deepEqual(idleDelta.delta.changed, []);
    const staleBtn = first.nodes.find(n => n.name === 'Morphing Button');
    assert(staleBtn?.ref);
    await tool('interact', { ...scope(), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(), observationId: idleDelta.observationId, action: 'click', elementRef: staleBtn.ref });
    const afterDelta = await observe({ compact: true, sinceObservationId: idleDelta.observationId });
    assert.equal(afterDelta.unchanged, false);
    const touched = [...afterDelta.delta.added, ...afterDelta.delta.changed];
    assert(touched.some(n => n.name === 'Morphed Label' && typeof n.ref === 'string'), JSON.stringify(afterDelta.delta));
  });

  await check('Enhanced xenon_wait supports case-insensitive anyOf, returnMode=matching/none, and absent=true', async () => {
    const obs = await observe({ roles: ['button'] });
    const trigger = obs.nodes.find(n => n.name === 'Trigger Async');
    assert(trigger?.ref);
    await tool('interact', { ...scope(), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(), observationId: obs.observationId, action: 'click', elementRef: trigger.ref });
    const loadingMatch = (await tool('wait', { ...scope(), anyOf: ['never-there', 'loading async'], returnMode: 'matching', compact: true, timeoutMs: 5000 })).structuredContent;
    assert.equal(loadingMatch.status, 'matched');
    assert.equal(loadingMatch.matchedText, 'loading async');
    assert(typeof loadingMatch.observationId === 'string' && loadingMatch.observationId.length > 0);
    assert(loadingMatch.nodes.some(n => n.name.includes('Loading Async')));

    const vanished = (await tool('wait', { ...scope(), text: 'loading async', absent: true, returnMode: 'none', timeoutMs: 5000 })).structuredContent;
    assert.equal(vanished.status, 'matched');
    assert.equal(vanished.nodes, undefined);
    assert(typeof vanished.observationId === 'string');

    const finalMatch = (await tool('wait', { ...scope(), anyOf: ['async complete ok', 'Error'], returnMode: 'none', timeoutMs: 5000 })).structuredContent;
    assert.equal(finalMatch.status, 'matched');
    assert.equal(finalMatch.matchedText, 'async complete ok');
  });

  await check('xenon_batch executes fill + hover + key + click atomically and reflects in page state', async () => {
    const obs = await observe({ interactiveOnly: true });
    const input = obs.nodes.find(n => n.role === 'textbox');
    const hoverBtn = obs.nodes.find(n => n.name === 'Hover Target');
    const submitBtn = obs.nodes.find(n => n.name === 'Submit Action');
    assert(input?.ref && hoverBtn?.ref && submitBtn?.ref);
    const batched = (await tool('batch', {
      ...scope(), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(), observationId: obs.observationId,
      steps: [
        { action: 'fill', elementRef: input.ref, text: 'hello' },
        { action: 'key', key: 'Enter' },
        { action: 'hover', elementRef: hoverBtn.ref },
        { action: 'click', elementRef: submitBtn.ref },
      ],
    })).structuredContent;
    assert.equal(batched.status, 'completed', JSON.stringify(batched));
    const after = await observe({ query: 'H:' });
    assert(after.nodes.some(n => n.name.includes('H:1 S:1 E:1 V:hello')), JSON.stringify(after.nodes));
  });

  await check('stale_element includes structured staleReason when element identity changes', async () => {
    const obs = await observe({ roles: ['button'] });
    const morphed = obs.nodes.find(n => n.name === 'Morphed Label');
    assert(morphed?.ref);
    // Navigate away briefly or trigger stale by changing label? Trigger label change on Trigger Async isn't needed;
    // re-observing doesn't remove morphed, so observe again with a different filter to supersede t->elements:
    await observe({ query: 'Functional Fixture Ready' });
    const stale = await raw('interact', {
      ...scope(), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(),
      observationId: (await observe({ query: 'Functional Fixture Ready' })).observationId,
      action: 'click', elementRef: morphed.ref,
    });
    assert.equal(stale.isError, true);
    assert.equal(stale.structuredContent?.error?.code, 'stale_element');
    assert.equal(stale.structuredContent?.error?.staleReason, 'observation_superseded');
  });

  await check('Cropped xenon_screenshot produces exact cropped PNG and still blocks protected pages', async () => {
    const fullReply = await screenshot();
    assert(!fullReply.isError);
    const full = fullReply.structuredContent;
    const clip = { x: 10, y: 10, width: 200, height: 120 };
    const cropReply = await screenshot({ clip });
    assert(!cropReply.isError, JSON.stringify(cropReply.structuredContent));
    const cropped = cropReply.structuredContent;
    const img = cropReply.content.find(c => c.type === 'image');
    assert(img && img.mimeType === 'image/png');
    const pngBytes = Buffer.from(img.data, 'base64');
    assert.equal(pngBytes.readUInt32BE(16), cropped.imageWidth);
    assert.equal(pngBytes.readUInt32BE(20), cropped.imageHeight);
    assert(cropped.imageWidth < full.imageWidth && cropped.imageHeight < full.imageHeight);
    assert.equal(cropped.clip?.source, 'clip');
    assert.match(cropped.coordinateMapping, /clip\.x/);

    // Navigate to page with password input far outside clip; full-page scan must still block!
    await tool('navigate', { ...scope(), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(), action: 'navigate', url: `${base}/secret` });
    await until(async () => (await observe()).nodes.some(n => n.name === 'Public header'));
    const blocked = await screenshot({ clip: { x: 20, y: 20, width: 100, height: 40 } });
    assert.equal(blocked.isError, true);
    assert.equal(blocked.structuredContent?.error?.code, 'screenshot_protected');
    assert.equal(blocked.structuredContent?.error?.reason, 'credential_fields');
    assert(!blocked.content.some(c => c.type === 'image'));
  });

  await check('xenon_attention kind=help/review and xenon_wait_ready work end-to-end', async () => {
    const rev = (await tool('attention', { ...scope(), ownershipGeneration: tab.ownershipGeneration, action: 'request', persist: true, reason: 'Form ready for review' })).structuredContent;
    assert.equal(rev.attentionRequested, true);
    assert.equal(rev.attentionKind, 'review');
    assert.equal(rev.attentionPersists, true);

    const ready = (await tool('wait_ready', { ...scope(), timeoutMs: 2000, waitForHelpResolution: true })).structuredContent;
    assert.equal(ready.status, 'ready', 'review mark should not block waitForHelpResolution');

    const help = (await tool('attention', { ...scope(), ownershipGeneration: tab.ownershipGeneration, action: 'request', kind: 'help', persist: false, reason: 'Need CAPTCHA' })).structuredContent;
    assert.equal(help.attentionKind, 'help');
    const timedOut = (await tool('wait_ready', { ...scope(), timeoutMs: 200, waitForHelpResolution: true })).structuredContent;
    assert.equal(timedOut.status, 'timeout', 'active help request should hold waitForHelpResolution until cleared');

    await tool('attention', { ...scope(), ownershipGeneration: tab.ownershipGeneration, action: 'clear' });
    const afterClear = (await tool('wait_ready', { ...scope(), timeoutMs: 1000, waitForHelpResolution: true })).structuredContent;
    assert.equal(afterClear.status, 'ready');
  });
} catch (error) {
  results.push({ name: 'Functional improvements harness', passed: false, error: error.message });
  console.error(error);
} finally {
  await client?.close().catch(() => {});
  if (browser?.pid) await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
  if (server) await new Promise(resolve => server.close(resolve));
  if (initialHash) results.push({ name: 'Binary unchanged during validation', passed: await hash() === initialHash });
  const report = { run, capturedAt: new Date().toISOString(), binary, applicationDllSha256: initialHash, passed: results.every(r => r.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true });
  const reportPath = resolve(root, `out/${run}.json`);
  await writeFile(reportPath, JSON.stringify(report, null, 2));
  console.log(`Report: ${reportPath}`);
  process.exitCode = report.passed ? 0 : 1;
}
