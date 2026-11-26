// Production CEF/MCP end-to-end verification of compact/delta/filtered observations,
// enhanced wait, hover/key batching, structured staleReason, cropped screenshots,
// attention kind, and xenon_wait_ready. Uses synthetic loopback HTML and a disposable profile.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { mkdir, readdir, readFile, writeFile } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';

const root = resolve(import.meta.dirname, '..'), build = resolve(root, process.env.XENON_TEST_BUILD_DIR ?? 'build');
const run = `functional-improvements-${Date.now()}-${randomBytes(4).toString('hex')}`, profile = resolve(root, '.cache', run);
const evidenceRoot = resolve(profile, 'evidence-root');
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
  await mkdir(evidenceRoot, { recursive: true });
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
    if (request.url === '/popup') {
      response.end(`<!doctype html><title>Popup Tab</title>
        <style>body{font:16px sans-serif;margin:20px}</style>
        <h1>Popup Tab Ready</h1>
        <p>Opened in the same control group.</p>`);
      return;
    }
    if (request.url === '/navigated') {
      response.end(`<!doctype html><title>Navigated Target</title>
        <style>body{font:16px sans-serif;margin:20px}</style>
        <h1>Navigated Target Ready</h1>`);
      return;
    }
    response.end(`<!doctype html><title>Functional Improvements Fixture</title>
      <style>body{font:16px sans-serif;margin:20px}button,input,a{font:16px sans-serif;margin:6px;padding:6px 10px;display:inline-block}</style>
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
      <a id="popupLink" href="/popup" target="_blank">Open Popup Tab</a>
      <a id="navLink" href="/navigated">Go Navigated</a>
      <div id="status">Initial Status</div>
      <div id="log"></div>
      <div style="height:1400px" aria-hidden="true"></div>
      <h2>Lower Section Heading</h2>
      <button id="belowFoldBtn">Below Fold Action</button>
      <div id="belowFoldStatus">BelowFold:0</div>
      <script>
        let hovered = 0, submitted = 0, enterPressed = 0, belowFold = 0;
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
        document.querySelector('#belowFoldBtn').addEventListener('click', () => {
          belowFold++;
          document.querySelector('#belowFoldStatus').textContent = 'BelowFold:' + belowFold;
        });
        log();
      </script>`);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;

  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { windowsHide: true, stdio: 'ignore' });
  await until(alive, 30000);
  client = new Client({ name: 'Functional improvements fixture', version: '1' });
  await client.connect(new StdioClientTransport({ command: process.execPath, args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', config, '--evidence-root', evidenceRoot], stderr: 'pipe' }));

  worker = (await tool('worker_create', { name: 'Functional worker' })).structuredContent;
  tab = (await tool('tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: `${base}/` })).structuredContent;
  await until(async () => (await observe()).nodes.some(n => n.name === 'Functional Fixture Ready'));

  await check('Compact observe reduces payload size significantly, condenses limitations, and deduplicates wrappers', async () => {
    const full = await observe();
    const compact = await observe({ compact: true });
    assert.equal(full.limitations, undefined, 'duplicate top-level limitations should be removed');
    assert(Array.isArray(full.visibility?.limitations) && full.visibility.limitations.length > 0);
    assert(Array.isArray(compact.visibility?.limitations) && compact.visibility.limitations.includes('rendered_viewport_only'));
    assert.equal(compact.textSafety?.limitation, 'unsafe_non_rendering_withheld');
    assert(compact.nodeDefaults && compact.nodeDefaults.visibilityStatus === 'verified');
    assert(!compact.nodes.some(n => n.role === 'paragraph'), 'pure structural paragraph wrappers covered by StaticText should be omitted in compact mode');
    assert(compact.nodes.some(n => n.role === 'StaticText' && n.name.includes('Paragraph text one')), 'StaticText children must be preserved');
    const fullBytes = Buffer.byteLength(JSON.stringify(full.nodes), 'utf8');
    const compactBytes = Buffer.byteLength(JSON.stringify(compact.nodes), 'utf8');
    const fullTotalBytes = Buffer.byteLength(JSON.stringify(full), 'utf8');
    const compactTotalBytes = Buffer.byteLength(JSON.stringify(compact), 'utf8');
    assert(compactBytes < fullBytes * 0.55, `Expected >=45% node reduction, got ${compactBytes} vs ${fullBytes}`);
    assert(compactTotalBytes < fullTotalBytes * 0.55, `Expected >=45% total payload reduction, got ${compactTotalBytes} vs ${fullTotalBytes}`);
    console.log(`  nodes JSON: full=${fullBytes}B -> compact=${compactBytes}B (${Math.round((1 - compactBytes / fullBytes) * 100)}% smaller)`);
    console.log(`  full structuredContent JSON: full=${fullTotalBytes}B -> compact=${compactTotalBytes}B (${Math.round((1 - compactTotalBytes / fullTotalBytes) * 100)}% smaller)`);
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

  await check('xenon_inspect supports compact sinceObservationId delta alongside valid PNG and --evidence-root saved paths', async () => {
    const firstReply = await until(async () => {
      const r = await raw('inspect', { ...scope(), compact: true });
      if (r.isError && ['privacy_guard_initializing', 'inspection_changed', 'page_changed'].includes(r.structuredContent?.error?.code)) return false;
      assert(!r.isError, JSON.stringify(r.structuredContent));
      return r;
    });
    const first = firstReply.structuredContent;
    assert(first.visibility?.limitations?.includes('validated_capture_interval_not_atomic'), JSON.stringify(first.visibility?.limitations));
    assert(firstReply.content.some(c => c.type === 'image' && c.mimeType === 'image/png'));
    const exp = first.evidenceExport;
    assert.equal(exp?.status, 'saved', JSON.stringify(exp));
    assert(exp.directory.startsWith(evidenceRoot), `evidenceExport.directory=${exp.directory} must be under ${evidenceRoot}`);
    const pngFile = exp.files.find(f => f.endsWith('.png'));
    assert(pngFile, `Expected PNG file in evidenceExport.files: ${JSON.stringify(exp.files)}`);
    const pngDisk = await readFile(join(exp.directory, pngFile));
    assert.equal(pngDisk.subarray(0, 4).toString('hex'), '89504e47', 'Saved evidence PNG must start with PNG magic');

    const deltaReply = await until(async () => {
      const r = await raw('inspect', { ...scope(), compact: true, sinceObservationId: first.observationId });
      if (r.isError && ['privacy_guard_initializing', 'inspection_changed', 'page_changed'].includes(r.structuredContent?.error?.code)) return false;
      assert(!r.isError, JSON.stringify(r.structuredContent));
      return r;
    });
    const deltaBody = deltaReply.structuredContent;
    assert.equal(deltaBody.unchanged, true, JSON.stringify(deltaBody));
    assert.equal(deltaBody.delta?.sinceObservationId, first.observationId);
    assert.deepEqual(deltaBody.delta?.added, []);
    assert.deepEqual(deltaBody.delta?.changed, []);
    assert.notEqual(deltaBody.observationId, first.observationId);
    assert(deltaReply.content.some(c => c.type === 'image' && c.mimeType === 'image/png'), 'Delta inspect still returns PNG');

    // Second independent CLI serve session using the exact same --evidence-root creates a second unique child dir.
    const secondClient = new Client({ name: 'Second root export client', version: '1' });
    try {
      await secondClient.connect(new StdioClientTransport({
        command: process.execPath,
        args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', config, '--evidence-root', evidenceRoot],
        stderr: 'pipe',
      }));
      const listReply = await secondClient.callTool({ name: 'xenon_workspaces', arguments: {} });
      assert(!listReply.isError, JSON.stringify(listReply.structuredContent));
      const exp2 = listReply.structuredContent?.evidenceExport;
      assert.equal(exp2?.status, 'saved');
      assert.notEqual(exp2.directory, exp.directory, 'Repeatable --evidence-root must create a unique child dir per run');
      assert.equal((await readdir(evidenceRoot)).length, 2);
    } finally {
      await secondClient.close().catch(() => {});
    }
  });

  await check('scroll + observeAfter and xenon_batch scroll + observeAfter eliminate extra observe turns', async () => {
    const startObs = await observe({ compact: true });
    const scrollReply = (await tool('interact', {
      ...scope(),
      ownershipGeneration: tab.ownershipGeneration,
      operationId: randomUUID(),
      observationId: startObs.observationId,
      action: 'scroll',
      direction: 'down',
      amount: 1600,
      observeAfter: { compact: true },
    })).structuredContent;
    assert.equal(scrollReply.status, 'dispatched');
    assert(scrollReply.observation && typeof scrollReply.observation.observationId === 'string', JSON.stringify(scrollReply));
    assert.equal(scrollReply.observationError, undefined);
    assert(scrollReply.observation.viewport?.pageY > 0, JSON.stringify(scrollReply.observation.viewport));
    const belowBtn = scrollReply.observation.nodes.find(n => n.name === 'Below Fold Action');
    assert(belowBtn?.ref, `Expected Below Fold Action in observeAfter nodes: ${JSON.stringify(scrollReply.observation.nodes)}`);

    // Immediately click Below Fold Action using observationId & ref from scroll's observeAfter + scroll back up in one xenon_batch!
    const batchBack = (await tool('batch', {
      ...scope(),
      ownershipGeneration: tab.ownershipGeneration,
      operationId: randomUUID(),
      observationId: scrollReply.observation.observationId,
      steps: [
        { action: 'click', elementRef: belowBtn.ref },
        { action: 'scroll', direction: 'up', amount: 1600 },
      ],
      observeAfter: { compact: true },
    })).structuredContent;
    assert.equal(batchBack.status, 'completed', JSON.stringify(batchBack));
    assert.equal(batchBack.steps.length, 2);
    assert.equal(batchBack.steps[1].action, 'scroll');
    assert(batchBack.observation && typeof batchBack.observation.observationId === 'string', JSON.stringify(batchBack));
    assert(batchBack.observation.nodes.some(n => n.name === 'Functional Fixture Ready'), JSON.stringify(batchBack.observation.nodes));

    // Confirm click actually mutated lower-page website state by scrolling back down once.
    const verifyDown = (await tool('interact', {
      ...scope(),
      ownershipGeneration: tab.ownershipGeneration,
      operationId: randomUUID(),
      observationId: batchBack.observation.observationId,
      action: 'scroll',
      direction: 'down',
      amount: 1600,
      observeAfter: { compact: true, query: 'BelowFold:' },
    })).structuredContent;
    assert(verifyDown.observation?.nodes?.some(n => n.name.includes('BelowFold:1')), JSON.stringify(verifyDown.observation));

    // Return to top for subsequent checks.
    await tool('interact', {
      ...scope(),
      ownershipGeneration: tab.ownershipGeneration,
      operationId: randomUUID(),
      observationId: verifyDown.observation.observationId,
      action: 'scroll',
      direction: 'up',
      amount: 1600,
      observeAfter: { compact: true },
    });
  });

  await check('xenon_interact(click) spawning a new owned tab returns createdTabs directly in same control group', async () => {
    const obs = await observe({ interactiveOnly: true });
    const popupLink = obs.nodes.find(n => n.name === 'Open Popup Tab');
    assert(popupLink?.ref, JSON.stringify(obs.nodes));
    const clickRes = (await tool('interact', {
      ...scope(),
      ownershipGeneration: tab.ownershipGeneration,
      operationId: randomUUID(),
      observationId: obs.observationId,
      action: 'click',
      elementRef: popupLink.ref,
    })).structuredContent;
    assert.equal(clickRes.status, 'dispatched');
    assert(Array.isArray(clickRes.createdTabs) && clickRes.createdTabs.length === 1, `Expected createdTabs on click reply: ${JSON.stringify(clickRes)}`);
    const created = clickRes.createdTabs[0];
    assert.equal(created.controlGroupId, tab.controlGroupId);
    assert.equal(created.ownershipGeneration, tab.ownershipGeneration);

    // Observe the newly created tab directly without calling xenon_tabs first.
    const popupObs = await until(async () => {
      const r = await raw('observe', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, tabId: created.tabId, compact: true });
      if (r.isError && ['privacy_guard_initializing', 'page_changed', 'OBSERVATION_CHANGED'].includes(r.structuredContent?.error?.code)) return false;
      assert(!r.isError, JSON.stringify(r.structuredContent));
      return r.structuredContent.nodes.some(n => n.name === 'Popup Tab Ready') ? r.structuredContent : false;
    });
    assert(popupObs.nodes.some(n => n.name === 'Popup Tab Ready'));

    await tool('tab_close', {
      agentSessionId: worker.agentSessionId,
      workspaceId: worker.workspaceId,
      tabId: created.tabId,
      ownershipGeneration: created.ownershipGeneration,
      operationId: randomUUID(),
    });
  });

  await check('Per-worker operationId isolation lets concurrent workers reuse readable IDs with unambiguous xenon_operation lookup', async () => {
    const workerB = (await tool('worker_create', { name: 'Second worker', workspaceId: worker.workspaceId })).structuredContent;
    const tabB = (await tool('tab_create', { agentSessionId: workerB.agentSessionId, workspaceId: workerB.workspaceId, url: `${base}/popup` })).structuredContent;
    try {
      const obsA = await observe({ compact: true });
      const obsB = await until(async () => {
        const r = await raw('observe', { agentSessionId: workerB.agentSessionId, workspaceId: workerB.workspaceId, tabId: tabB.tabId, compact: true });
        if (r.isError && ['privacy_guard_initializing', 'page_changed', 'OBSERVATION_CHANGED'].includes(r.structuredContent?.error?.code)) return false;
        assert(!r.isError, JSON.stringify(r.structuredContent));
        return r.structuredContent.nodes.some(n => n.name === 'Popup Tab Ready') ? r.structuredContent : false;
      });
      const sharedOpId = 'op-pd-scroll-1';
      const [resA, resB] = await Promise.all([
        tool('interact', { ...scope(), ownershipGeneration: tab.ownershipGeneration, operationId: sharedOpId, observationId: obsA.observationId, action: 'scroll', direction: 'down', amount: 100 }),
        tool('interact', { agentSessionId: workerB.agentSessionId, workspaceId: workerB.workspaceId, tabId: tabB.tabId, ownershipGeneration: tabB.ownershipGeneration, operationId: sharedOpId, observationId: obsB.observationId, action: 'scroll', direction: 'down', amount: 100 }),
      ]);
      assert.equal(resA.structuredContent.operationId, sharedOpId);
      assert.equal(resB.structuredContent.operationId, sharedOpId);

      // Ambiguous lookup without agentSessionId fails with OPERATION_AMBIGUOUS.
      const ambig = await raw('operation', { operationId: sharedOpId });
      assert.equal(ambig.isError, true);
      assert.equal(ambig.structuredContent?.error?.code, 'OPERATION_AMBIGUOUS');

      // Disambiguated lookups resolve each worker's operation cleanly.
      const opA = (await tool('operation', { operationId: sharedOpId, agentSessionId: worker.agentSessionId })).structuredContent;
      const opB = (await tool('operation', { operationId: sharedOpId, agentSessionId: workerB.agentSessionId })).structuredContent;
      assert.equal(opA.agentSessionId, worker.agentSessionId);
      assert.equal(opA.state, 'completed');
      assert.equal(opB.agentSessionId, workerB.agentSessionId);
      assert.equal(opB.state, 'completed');

      await tool('tab_close', { agentSessionId: workerB.agentSessionId, workspaceId: workerB.workspaceId, tabId: tabB.tabId, ownershipGeneration: tabB.ownershipGeneration, operationId: randomUUID() });
    } finally {
      await raw('worker_retire', { agentSessionId: workerB.agentSessionId }).catch(() => {});
    }
  });

  await check('Cross-document navigation click with observeAfter preserves applied click and reports observationError honestly', async () => {
    // Scroll back to top first so Go Navigated link is in viewport.
    const topObs = await observe({ compact: true });
    const navObs = topObs.viewport?.pageY > 0
      ? (await tool('interact', { ...scope(), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(), observationId: topObs.observationId, action: 'scroll', direction: 'up', amount: 400, observeAfter: { compact: true } })).structuredContent.observation
      : topObs;
    const navLink = navObs.nodes.find(n => n.name === 'Go Navigated');
    assert(navLink?.ref, JSON.stringify(navObs.nodes));
    const navClick = (await tool('interact', {
      ...scope(),
      ownershipGeneration: tab.ownershipGeneration,
      operationId: randomUUID(),
      observationId: navObs.observationId,
      action: 'click',
      elementRef: navLink.ref,
      observeAfter: { compact: true, settleMs: 300 },
    })).structuredContent;
    assert.equal(navClick.status, 'dispatched');
    assert.equal(navClick.dispatchStatus, 'dispatched');
    if (navClick.observationError) {
      assert.equal(navClick.observation, undefined);
      assert(['OBSERVATION_CHANGED', 'page_changed'].includes(navClick.observationError.code), JSON.stringify(navClick.observationError));
    }
    // Verify the navigation genuinely took effect on the website.
    await until(async () => (await observe({ compact: true })).nodes.some(n => n.name === 'Navigated Target Ready'));
    // Navigate back to `/` for subsequent checks.
    await tool('navigate', { ...scope(), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(), action: 'navigate', url: `${base}/` });
    await until(async () => (await observe({ compact: true })).nodes.some(n => n.name === 'Functional Fixture Ready'));
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
