// Real CEF + named pipe + real modern/legacy MCP clients. Uses only generated
// test profiles and synthetic pairings. Never reads an existing browser profile.
import assert from 'node:assert/strict';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { randomBytes, randomUUID, createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';

const root = resolve(import.meta.dirname, '..');
const run = `integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const pipeName = `xenon-test-${run}`;
const pipe = '\\\\.\\pipe\\' + pipeName;
const profile = resolve(root, '.cache', run);
const applicationDllSha256 = createHash('sha256').update(await readFile(resolve(root, 'build/app/Release/Xenon.dll'))).digest('hex');
const base = 'http://127.0.0.1:18765';
const results = [];
const clients = [];
const lastObservations = new Map();
const sleep = ms => new Promise(r => setTimeout(r, ms));
const runFile = promisify(execFile);
const alivePipe = () => new Promise(resolve => {
  const s = net.connect(pipe);
  s.once('connect', () => { s.destroy(); resolve(true); });
  s.once('error', () => resolve(false));
});
async function until(fn, ms = 15000) {
  const end = Date.now() + ms;
  while (Date.now() < end) { const value = await fn(); if (value) return value; await sleep(150); }
  throw new Error('Condition timed out');
}
async function check(name, fn) {
  const start = Date.now();
  try { const detail = await fn(); results.push({ name, passed: true, elapsedMs: Date.now() - start, detail }); console.log(`PASS ${name}`); }
  catch (error) { results.push({ name, passed: false, elapsedMs: Date.now() - start, error: error.message }); console.log(`FAIL ${name}: ${error.message}`); }
}
async function raw(client, name, args = {}) { return client.callTool({ name: `xenon_${name}`, arguments: args }); }
async function tool(client, name, args = {}) {
  const response = await raw(client, name, args);
  if (response.isError) throw new Error(`${name}: ${JSON.stringify(response.structuredContent ?? response.content)}`);
  return response.structuredContent;
}
const scope = tab => ({ agentSessionId: tab.agentSessionId, workspaceId: tab.workspaceId, tabId: tab.tabId });
const workspaceScope = worker => ({ agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId });
const mutation = tab => ({ ...scope(tab), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID() });
async function observe(client, tab) {
  const deadline = Date.now() + 5000;
  while (true) {
    const response = await raw(client, 'observe', { ...scope(tab), maxNodes: 1000 });
    lastObservations.set(tab.tabId, response);
    if (response.isError && response.structuredContent?.error?.code === 'privacy_guard_initializing' && Date.now() < deadline) {
      // A newly attached frame can restart the document privacy guard. Retry
      // only this explicitly transient read; never replay any mutation.
      await sleep(100); continue;
    }
    if (response.isError) throw new Error(`observe: ${JSON.stringify(response.structuredContent ?? response.content)}`);
    return response.structuredContent;
  }
}
async function ready(client, tab) {
  return until(async () => {
    try { const obs = await observe(client, tab); return obs.nodes?.some(n => n.name === 'Ready for concurrent work') ? obs : false; }
    catch { return false; }
  });
}
async function scrollObserved(client, tab, observation, direction, amount) {
  await tool(client, 'interact', { ...mutation(tab), observationId: observation.observationId, action: 'scroll', direction, amount });
  // Mouse-wheel completion precedes compositor layout becoming observable.
  await sleep(120);
  return observe(client, tab);
}
async function findVisible(client, tab, predicate, description) {
  let observation = await observe(client, tab);
  if (predicate(observation)) return observation;
  // Use only bounded, real MCP scrolling. Page evidence itself never scrolls.
  for (let attempt = 0; attempt < 3 && (observation.viewport?.pageY === undefined || observation.viewport.pageY > 0); attempt++)
    observation = await scrollObserved(client, tab, observation, 'up', 3000);
  for (let attempt = 0; attempt < 16; attempt++) {
    if (predicate(observation)) return observation;
    const priorY = observation.viewport?.pageY;
    const amount = Math.max(120, Math.min(500, Math.floor((observation.viewport?.height ?? 600) * 0.55)));
    observation = await scrollObserved(client, tab, observation, 'down', amount);
    if (priorY !== undefined && observation.viewport?.pageY === priorY) break;
  }
  throw new Error(`No rendered viewport target after bounded scrolling: ${description}`);
}
async function act(client, tab, action, nodeName, fields = {}) {
  const expectedRole = action === 'fill' ? 'textbox' : action === 'click' ? 'button' : undefined;
  const obs = nodeName ? await findVisible(client, tab,
    observation => observation.nodes.some(node => node.name === nodeName && node.ref && (!expectedRole || node.role === expectedRole)) ||
      (action === 'click' && observation.nodes.some(node => node.name === nodeName && node.ref && node.role === 'link')), nodeName) : await observe(client, tab);
  const candidates = nodeName ? obs.nodes.filter(n => n.name === nodeName && n.ref) : [];
  const ref = (candidates.find(n => n.role === expectedRole) ?? candidates[0])?.ref;
  if (nodeName && !ref) throw new Error(`No actionable reference: ${nodeName}`);
  return tool(client, 'interact', { ...mutation(tab), observationId: obs.observationId, action, ...(ref ? { elementRef: ref } : {}), ...fields });
}
async function telemetry(caseId) { return (await fetch(`${base}/telemetry?id=${encodeURIComponent(caseId)}`)).json(); }
async function connect(index, era = 'modern') {
  const wire = new StdioClientTransport({ command: process.execPath, args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', resolve(profile, `client-${index}.json`)], stderr: 'pipe' });
  const client = new Client({ name: `Xenon live ${index}`, version: '1' }, { versionNegotiation: { mode: era === 'legacy' ? 'legacy' : { pin: '2026-07-28' } } });
  await client.connect(wire); clients.push(client); return client;
}
if (process.platform !== 'win32') throw new Error('Live CEF tests require Windows.');
if (await alivePipe()) throw new Error('The randomly allocated integration test pipe is already in use.');
await mkdir(profile, { recursive: true });
const fixtures = Array.from({ length: 4 }, (_, i) => ({ clientId: `fixture_client_${i}`, token: randomBytes(32).toString('hex') }));
await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1, clients: fixtures.map((c, i) => ({ id: c.clientId, name: `Integration ${i}`, tokenHash: createHash('sha256').update(c.token).digest('hex') })), workspaces: [{ id: 'fixture_shared', clients: fixtures.map(c => c.clientId) }], accountGrants: [], operations: [] }));
for (let i = 0; i < fixtures.length; i++) await writePrivateConfig(resolve(profile, `client-${i}.json`), { ...fixtures[i], pipe });
const server = spawn(process.execPath, [resolve(root, 'tests/fixtures/server.mjs')], { windowsHide: true, stdio: 'ignore' });
const browser = spawn(resolve(root, 'build/app/Release/Xenon.exe'), [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { windowsHide: true, stdio: 'ignore' });
try {
  await until(alivePipe, 30000);
  await until(async () => { try { return (await fetch(`${base}/health`)).ok; } catch { return false; } });
  const separate = await Promise.all(fixtures.map((_, i) => connect(i, i % 2 ? 'legacy' : 'modern')));
  const separateTabs = [];
  await check('Four separate adapters, three live tabs each', async () => {
    await Promise.all(separate.map(async (client, i) => {
      const worker = await tool(client, 'worker_create', { name: `separate-${i}` });
      const tabs = await Promise.all(Array.from({ length: 3 }, async (_, j) => {
        const caseId = `${run}-separate-${i}-${j}`;
        const created = await tool(client, 'tab_create', { ...workspaceScope(worker), url: `${base}/?case=${caseId}` });
        const tab = { ...worker, ...created, caseId, client };
        separateTabs.push(tab); await ready(client, tab);
        await act(client, tab, 'click', 'Increment count');
        await until(async () => (await observe(client, tab)).nodes?.some(n => n.name === 'Count: 1'));
        return tab;
      }));
      assert.equal(tabs.length, 3);
    }));
    assert.equal(separateTabs.length, 12);
  });
  await check('Workspace and result isolation', async () => {
    const first = separateTabs.find(t => t.client === separate[0]);
    const second = separateTabs.find(t => t.client === separate[1]);
    assert(first && second);
    const stolen = await raw(separate[1], 'observe', { ...scope(first), agentSessionId: second.agentSessionId });
    assert.equal(stolen.isError, true);
    const workspaces = await tool(separate[1], 'workspaces');
    assert(!workspaces.workspaces.some(w => w.workspaceId === first.workspaceId));
    await tool(separate[0], 'navigate', { ...mutation(first), action: 'navigate', url: `${base}/cookie?set=isolated-${run}` });
    await until(async () => { try { return (await observe(separate[0], first)).url.includes('/cookie'); } catch { return false; } });
    await tool(separate[0], 'navigate', { ...mutation(first), action: 'navigate', url: `${base}/?case=${first.caseId}` });
    await ready(separate[0], first);
    await until(async () => (await telemetry(first.caseId))?.cookie.includes(run));
    assert(!(await telemetry(second.caseId)).cookie.includes(run));
  });
  await check('A pending wait does not block other adapters', async () => {
    const stalled = separateTabs[0];
    assert(stalled);
    const wait = raw(stalled.client, 'wait', { ...scope(stalled), text: 'Text intentionally never present', timeoutMs: 4000 });
    const start = Date.now();
    const others = separateTabs.filter(t => t.client !== stalled.client);
    await Promise.all(others.map(t => observe(t.client, t)));
    assert(Date.now() - start < 3500, 'Independent evidence was blocked by a different tab wait');
    await wait;
  });
  await check('Page-only screenshots through real MCP image output', async () => {
    const tab = separateTabs[0]; const result = await raw(tab.client, 'screenshot', scope(tab));
    assert(!result.isError, JSON.stringify(result.structuredContent));
    const img = result.content.find(c => c.type === 'image'); assert(img && img.data.length > 100);
    assert.equal(img.mimeType, 'image/png');
    assert(!JSON.stringify(result.structuredContent).includes(img.data));
    const png=Buffer.from(img.data, 'base64'), meta=result.structuredContent;
    assert.equal(meta.imageWidth,png.readUInt32BE(16));assert.equal(meta.imageHeight,png.readUInt32BE(20));
    assert.equal(meta.tabId,tab.tabId);assert.equal(meta.workspaceId,tab.workspaceId);
    assert(Math.abs(meta.scaleX*meta.viewport.width-meta.imageWidth)<0.01);
    assert(Math.abs(meta.scaleY*meta.viewport.height-meta.imageHeight)<0.01);
    await writeFile(resolve(profile, 'fixture-page.png'), png);
  });
  const caseWorker = await tool(separate[3], 'worker_create', { name: 'Dynamic controls' });
  const caseTab = { ...caseWorker, ...await tool(separate[3], 'tab_create', { ...workspaceScope(caseWorker), url: `${base}/?case=${run}-controls` }) };
  await ready(separate[3], caseTab);
  await check('Replaced nodes reject stale references', async () => {
    const evidence = await findVisible(separate[3], caseTab, observation => ['Replace target', 'Original target'].every(name => observation.nodes.some(node => node.role === 'button' && node.name === name && node.ref)), 'replacement controls');
    const replacement = evidence.nodes.find(n => n.role === 'button' && n.name === 'Replace target');
    const prior = evidence.nodes.find(n => n.role === 'button' && n.name === 'Original target');
    assert(replacement && prior);
    await tool(separate[3], 'interact', { ...mutation(caseTab), observationId: evidence.observationId, action: 'click', elementRef: replacement.ref });
    const stale = await raw(separate[3], 'interact', { ...mutation(caseTab), observationId: evidence.observationId, action: 'click', elementRef: prior.ref });
    assert(stale.isError, 'Replaced target accepted');
  });
  await check('Overlay hit testing rejects obscured input', async () => {
    const before = await findVisible(separate[3], caseTab, observation => ['Increment count', 'Show overlay'].every(name => observation.nodes.some(node => node.role === 'button' && node.name === name && node.ref)), 'overlay trigger and target');
    const target = before.nodes.find(n => n.role === 'button' && n.name === 'Increment count');
    const trigger = before.nodes.find(n => n.role === 'button' && n.name === 'Show overlay');
    await tool(separate[3], 'interact', { ...mutation(caseTab), observationId: before.observationId, action: 'click', elementRef: trigger.ref });
    const evidence = await observe(separate[3], caseTab);
    assert(!evidence.nodes.some(n => n.role === 'button' && n.name === 'Increment count'), 'Obscured target leaked into rendered evidence');
    const blocked = await raw(separate[3], 'interact', { ...mutation(caseTab), observationId: before.observationId, action: 'click', elementRef: target.ref });
    assert(blocked.isError, 'Obscured button accepted');
    await act(separate[3], caseTab, 'click', 'Close overlay');
  });
  await check('Same-origin and cross-origin frames expose usable references', async () => {
    for (const label of ['Same origin', 'Cross origin']) {
      await act(separate[3], caseTab, 'click', `${label} frame button`);
      assert((await observe(separate[3], caseTab)).nodes.some(n => n.name === `${label} frame clicked`));
    }
  });
  await check('Shadow DOM actionability and semantics', async () => {
    await act(separate[3], caseTab, 'click', 'Shadow button');
    assert((await observe(separate[3], caseTab)).nodes.some(n => n.name === 'Shadow clicked'));
  });
  await check('Visual coordinates require a current screenshot', async () => {
    const isCanvas = node => node.bounds && (node.role?.toLowerCase() === 'canvas' || node.tagName?.toLowerCase() === 'canvas' || node.tag?.toLowerCase() === 'canvas');
    const evidence = await findVisible(separate[3], caseTab, observation => observation.nodes.some(isCanvas), 'visible canvas');
    const canvas = evidence.nodes.find(isCanvas); assert(canvas);
    assert(!canvas.name || canvas.name.toLowerCase() === 'canvas', 'Canvas must not expose a machine-only aria-label');
    const denied = await raw(separate[3], 'interact', { ...mutation(caseTab), observationId: evidence.observationId, action: 'click', x: 10, y: 10 });
    assert(denied.isError, 'Ordinary semantic observation authorized coordinate click');
    const shot = await tool(separate[3], 'screenshot', scope(caseTab));
    await tool(separate[3], 'interact', { ...mutation(caseTab), observationId: shot.observationId, action: 'click', x: canvas.bounds.x + 60 - shot.viewport.pageX, y: canvas.bounds.y + 40 - shot.viewport.pageY });
    assert((await observe(separate[3], caseTab)).nodes.some(n => n.name === 'Canvas clicked'));
  });
  await check('JavaScript dialogs are handled without blocking other tabs', async () => {
    const evidence = await findVisible(separate[3], caseTab, observation => observation.nodes.some(n => n.role === 'button' && n.name === 'Open dialog' && n.ref), 'dialog trigger');
    const button = evidence.nodes.find(n=>n.role==='button' && n.name==='Open dialog'); assert(button);
    const pending = raw(separate[3], 'interact', { ...mutation(caseTab), observationId: evidence.observationId, action: 'click', elementRef: button.ref });
    // Native cursor synchronization and bounded movement precede mouse-down.
    await sleep(800);
    await observe(separateTabs[0].client, separateTabs[0]);
    // The original input can have an uncertain outcome while script is paused.
    // Dismiss the known fixture dialog, never repeat the click.
    await tool(separate[3], 'dialog', { ...mutation(caseTab), accept: false });
    const click = await pending;
    if(click.isError) assert.equal(click.structuredContent.dispatchStatus, 'dispatched');
    await observe(separate[3], caseTab);
  });
  await check('Website JavaScript links and logout confirmations remain usable', async () => {
    await act(separate[3], caseTab, 'click', 'Script logout link');
    await until(async () => (await observe(separate[3], caseTab)).nodes.some(node => node.name === 'Script logout link executed'));
    for (const accept of [false, true]) {
      const evidence = await findVisible(separate[3], caseTab, observation => observation.nodes.some(node => node.role === 'button' && node.name === 'Confirm synthetic logout' && node.ref), 'confirmation trigger');
      const button = evidence.nodes.find(node => node.role === 'button' && node.name === 'Confirm synthetic logout' && node.ref);
      const pending = raw(separate[3], 'interact', { ...mutation(caseTab), observationId: evidence.observationId, action: 'click', elementRef: button.ref });
      await sleep(800);
      await observe(separateTabs[0].client, separateTabs[0]);
      await tool(separate[3], 'dialog', { ...mutation(caseTab), accept });
      const click = await pending;
      if (click.isError) assert.equal(click.structuredContent.dispatchStatus, 'dispatched');
      const expected = accept ? 'Synthetic logout confirmed' : 'Synthetic logout cancelled';
      await until(async () => (await observe(separate[3], caseTab)).nodes.some(node => node.name === expected));
    }
    return { scriptLinkExecuted: true, confirmationAcceptedAndDismissed: true };
  });
  await check('Download namespace reports opaque scoped metadata', async () => {
    const before = await tool(separate[3], 'downloads', workspaceScope(caseWorker));
    await act(separate[3], caseTab, 'scroll', undefined, { direction: 'down', amount: 420 });
    await act(separate[3], caseTab, 'click', 'Download fixture report');
    const after = await until(async () => { const r = await tool(separate[3], 'downloads', workspaceScope(caseWorker)); return r.downloads.length > before.downloads.length ? r : false; });
    assert(!JSON.stringify(after).includes(profile), 'Native path leaked');
    const other = await tool(separate[0], 'downloads', { agentSessionId: separateTabs.find(t=>t.client===separate[0]).agentSessionId, workspaceId: separateTabs.find(t=>t.client===separate[0]).workspaceId });
    assert(!other.downloads.some(d => after.downloads.some(a => a.fileId && a.fileId === d.fileId)));
  });
  const sharedA = await tool(separate[0], 'worker_create', { name: 'Handoff sender', workspaceId: 'fixture_shared' });
  const sharedB = await tool(separate[1], 'worker_create', { name: 'Handoff recipient', workspaceId: 'fixture_shared' });
  const handoffCase = `${run}-handoff`;
  const sharedTab = { ...sharedA, ...await tool(separate[0], 'tab_create', { ...workspaceScope(sharedA), url: `${base}/?case=${handoffCase}` }) };
  await ready(separate[0], sharedTab);
  await check('Idle agent handoff preserves live document, form, selection, scroll, stream and site events', async () => {
    await act(separate[0], sharedTab, 'fill', 'Draft', { text: 'Unsubmitted live handoff draft' });
    await act(separate[0], sharedTab, 'key', undefined, { key: 'Shift+ArrowLeft' });
    await act(separate[0], sharedTab, 'scroll', undefined, { direction: 'down', amount: 320 });
    await sleep(700);
    const old = await observe(separate[0], sharedTab);
    const before = await telemetry(handoffCase);
    const moved = await tool(separate[0], 'control', { ...scope(sharedTab), action: 'handoff', expectedGeneration: sharedTab.ownershipGeneration, toSessionId: sharedB.agentSessionId });
    assert.equal(moved.ownerSessionId, sharedB.agentSessionId);
    assert.equal(moved.ownershipGeneration, sharedTab.ownershipGeneration + 1);
    Object.assign(sharedTab, sharedB, moved);
    await sleep(700);
    const after = await telemetry(handoffCase);
    for (const k of ['timeOrigin','draft','active','selectionStart','selectionEnd','scrollX','scrollY','count']) assert.deepEqual(after[k], before[k], k);
    assert.deepEqual(after.events, before.events, 'Ownership emitted website input/focus/navigation/storage events');
    assert(Number(after.stream.split(' ').at(-1)) > Number(before.stream.split(' ').at(-1)), 'WebSocket stopped');
    const stale = await raw(separate[1], 'interact', { ...mutation(sharedTab), observationId: old.observationId, action: 'key', key: 'ArrowLeft' });
    assert(stale.isError, 'Recipient reused sender evidence');
    const fresh = await observe(separate[1], sharedTab); assert.equal(fresh.documentId, old.documentId);
    return { documentPreserved: true, websiteEventsAdded: 0 };
  });
  await check('Related popups transfer as one ownership group', async () => {
    await act(separate[1], sharedTab, 'scroll', undefined, { direction: 'up', amount: 3000 });
    await sleep(150);
    const before = await tool(separate[1], 'tabs', {agentSessionId:sharedB.agentSessionId,workspaceId:sharedB.workspaceId});
    await act(separate[1], sharedTab, 'click', 'Open popup');
    const popup = await until(async()=>{
      const after=await tool(separate[1], 'tabs', {agentSessionId:sharedB.agentSessionId,workspaceId:sharedB.workspaceId});
      return after.tabs.find(t=>!before.tabs.some(old=>old.tabId===t.tabId));
    });
    assert.equal(popup.ownerSessionId, sharedB.agentSessionId);
    assert.equal(popup.controlGroupId, sharedTab.controlGroupId);
    const moved=await tool(separate[1],'control',{...scope(sharedTab),action:'handoff',expectedGeneration:sharedTab.ownershipGeneration,toSessionId:sharedA.agentSessionId});
    Object.assign(sharedTab,sharedA,moved);
    const all=await tool(separate[0],'tabs',{agentSessionId:sharedA.agentSessionId,workspaceId:sharedA.workspaceId});
    const after=all.tabs.find(t=>t.tabId===popup.tabId);assert(after);
    assert.equal(after.ownerSessionId,sharedA.agentSessionId);assert.equal(after.ownershipGeneration,sharedTab.ownershipGeneration);
    // Close through the new owner; closing the popup must not close the opener.
    await tool(separate[0],'tab_close',{...mutation({...sharedA,...after})});
  });
  await check('Mid-drag handoff waits for release before changing ownership', async () => {
    const evidence=await findVisible(separate[0],sharedTab, observation => ['Drag handle','Drag destination'].every(name=>observation.nodes.some(n=>n.role==='button'&&n.name===name&&n.ref)), 'both drag endpoints');
    const from=evidence.nodes.find(n=>n.name==='Drag handle'&&n.role==='button');
    const to=evidence.nodes.find(n=>n.name==='Drag destination'&&n.role==='button');assert(from&&to);
    const pending=raw(separate[0],'interact',{...mutation(sharedTab),observationId:evidence.observationId,action:'drag',fromRef:from.ref,toRef:to.ref});
    const deadline=Date.now()+5000;
    while((await(await fetch(`${base}/gesture?id=${encodeURIComponent(handoffCase)}`)).json()).phase!=='down') {
      if(Date.now()>deadline)throw new Error('Drag never delivered pointer-down'); await sleep(5);
    }
    const transferring=await tool(separate[0],'control',{...scope(sharedTab),action:'handoff',expectedGeneration:sharedTab.ownershipGeneration,toSessionId:sharedB.agentSessionId});
    assert.equal(transferring.handoffPending,true);assert.equal(transferring.ownerSessionId,sharedA.agentSessionId);
    const gesture=await pending;assert(!gesture.isError,JSON.stringify(gesture.structuredContent));
    const committed=await until(async()=>{const s=await tool(separate[1],'control_status',{...scope(sharedTab),agentSessionId:sharedB.agentSessionId});return s.ownerSessionId===sharedB.agentSessionId?s:false;});
    Object.assign(sharedTab,sharedB,committed);
    // Endpoint visibility does not imply that the following result label fits
    // in the viewport. Use the recipient's fresh evidence and normal scrolling
    // after ownership has committed; still assert the actual website outcome.
    const result=await findVisible(separate[1],sharedTab,observation=>observation.nodes.some(n=>n.name==='Drag completed'||n.name==='Drag released'),'drag result');
    assert(result.nodes.some(n=>n.name==='Drag completed'));
  });
  await check('Four workers behind one adapter, three tabs each', async () => {
    const multiplexed = await Promise.all(Array.from({ length: 4 }, async (_, i) => {
      const worker = await tool(separate[2], 'worker_create', { name: `multiplexed-${i}` });
      return Promise.all(Array.from({ length: 3 }, async (_, j) => {
        const caseId = `${run}-multiplexed-${i}-${j}`;
        const tab = { ...worker, ...await tool(separate[2], 'tab_create', { ...workspaceScope(worker), url: `${base}/?case=${caseId}` }) };
        await ready(separate[2], tab); await act(separate[2], tab, 'click', 'Increment count');
        await until(async () => (await observe(separate[2], tab)).nodes?.some(n => n.name === 'Count: 1')); return tab;
      }));
    }));
    assert.equal(multiplexed.flat().length, 12);
  });
  await check('Disconnect/reconnect preserves page and never replays mutations', async () => {
    const prior = await observe(separate[1], sharedTab); await separate[1].close(); await sleep(800);
    const reconnected = await connect(1);
    await tool(reconnected, 'worker_resume', { agentSessionId: sharedB.agentSessionId });
    const current = await tool(reconnected, 'control_status', scope(sharedTab));
    const owned = await tool(reconnected, 'control', { ...scope(sharedTab), action: 'acquire', expectedGeneration: current.ownershipGeneration });
    Object.assign(sharedTab, owned);
    const resumed = await observe(reconnected, sharedTab); assert.equal(resumed.documentId, prior.documentId);
    assert.equal((await telemetry(handoffCase)).draft, 'Unsubmitted live handoff draft');
  });
} catch (error) { results.push({ name: 'Integration harness', passed: false, error: error.message }); console.log(`FAIL harness: ${error.message}`); }
finally {
  // Generated fixture evidence only, never a real browsing profile.
  try { await writeFile(resolve(profile, 'last-observations.json'), JSON.stringify(Object.fromEntries(lastObservations), null, 2)); }
  catch (error) { results.push({ name: 'Synthetic observation diagnostic', passed: false, error: error.message }); }
  try { await writeFile(resolve(profile, 'telemetry.json'), JSON.stringify(await (await fetch(`${base}/telemetry?all`)).json(), null, 2)); } catch {}
  for (const client of clients) await client.close().catch(() => {});
  // Kill only the process tree created with this unique disposable test profile.
  if (browser.pid) await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
  server.kill();
  const finalHash=createHash('sha256').update(await readFile(resolve(root,'build/app/Release/Xenon.dll'))).digest('hex');
  if(finalHash!==applicationDllSha256)results.push({name:'Binary remained unchanged during test run',passed:false,error:'Application was rebuilt during validation'});
  const report = { run, capturedAt: new Date().toISOString(), binary: 'build/app/Release/Xenon.exe', applicationDllSha256, profile, passed: results.every(r => r.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true });
  await writeFile(resolve(root, 'out/integration-results.json'), JSON.stringify(report, null, 2));
  console.log(`Report: ${resolve(root, 'out/integration-results.json')}`);
  process.exitCode = report.passed ? 0 : 1;
}
