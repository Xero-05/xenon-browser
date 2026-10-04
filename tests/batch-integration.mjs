// Real modern/legacy MCP, production CEF, synthetic pages and disposable profiles.
import assert from 'node:assert/strict';
import http from 'node:http';
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
const run = `batch-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile = resolve(root, '.cache', run);
const pipeName = `xenon-test-${run}`, pipe = '\\\\.\\pipe\\' + pipeName;
const binary = resolve(root, 'build/app/Release/Xenon.exe');
const dll = resolve(root, 'build/app/Release/Xenon.dll');
const digest = async () => createHash('sha256').update(await readFile(dll)).digest('hex');
const applicationDllSha256 = await digest();
const html = await readFile(resolve(root, 'tests/fixtures/batch.html'));
const states = new Map(), destinations = new Set(), results = [], clients = [];
const sleep = ms => new Promise(r => setTimeout(r, ms));
const runFile = promisify(execFile);
const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://127.0.0.1');
  res.setHeader('Cache-Control', 'no-store');
  if (url.pathname === '/state' && req.method === 'POST') {
    let body = '';
    req.on('data', chunk => { body += chunk; if (body.length > 65_536) req.destroy(); });
    req.on('end', () => { try { states.set(url.searchParams.get('id'), JSON.parse(body)); res.end('ok'); } catch { res.writeHead(400).end(); } });
  } else if (url.pathname === '/destination') {
    destinations.add(url.searchParams.get('case')); res.setHeader('Content-Type', 'text/html');
    res.end('<!doctype html><title>Batch destination</title><h1>Batch destination reached</h1>');
  } else { res.setHeader('Content-Type', 'text/html; charset=utf-8'); res.end(html); }
});
async function until(fn, timeout = 15_000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) { const value = await fn(); if (value) return value; await sleep(100); }
  throw new Error('Condition timed out');
}
async function check(name, fn) {
  const start = performance.now();
  try { const detail = await fn(); results.push({ name, passed: true, elapsedMs: Math.round(performance.now() - start), detail }); console.log(`PASS ${name}`); }
  catch (error) { results.push({ name, passed: false, error: error.message }); console.log(`FAIL ${name}: ${error.message}`); }
}
const scope = tab => ({ agentSessionId: tab.agentSessionId, workspaceId: tab.workspaceId, tabId: tab.tabId });
const mutation = tab => ({ ...scope(tab), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID() });
const raw = (client, name, args = {}) => client.callTool({ name: `xenon_${name}`, arguments: args });
async function tool(client, name, args = {}) {
  const reply = await raw(client, name, args);
  assert(!reply.isError, `${name}: ${JSON.stringify(reply.structuredContent)}`);
  return reply.structuredContent;
}
const alivePipe = () => new Promise(resolve => {
  const socket = net.connect(pipe);
  socket.once('connect', () => { socket.destroy(); resolve(true); });
  socket.once('error', () => resolve(false));
});
async function observe(client, tab) { return tool(client, 'observe', { ...scope(tab), maxNodes: 1000 }); }
async function ready(client, worker, base, name) {
  const caseId = `${run}-${name}`;
  const created = await tool(client, 'tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: `${base}/?case=${caseId}` });
  const tab = { ...worker, ...created, caseId };
  const evidence = await until(async () => {
    try { const obs = await observe(client, tab); return obs.nodes.some(n => n.name === 'Batch fixture ready') ? obs : false; } catch { return false; }
  });
  return { tab, evidence };
}
function reference(obs, name, tag) {
  const node = obs.nodes.find(n => n.name === name && n.ref && (!tag || n.tag === tag));
  assert(node, `Missing rendered target ${name}: ${JSON.stringify(obs.nodes.map(n => ({ name: n.name, tag: n.tag })))}`);
  return node.ref;
}
function plan(obs) {
  return [
    { action: 'fill', elementRef: reference(obs, 'First field', 'INPUT'), text: 'Synthetic first' },
    { action: 'fill', elementRef: reference(obs, 'Second field', 'TEXTAREA'), text: 'Synthetic second' },
    { action: 'fill', elementRef: reference(obs, 'Fixture date', 'INPUT'), text: '2026-10-03' },
    { action: 'select', elementRef: reference(obs, 'Priority', 'SELECT'), values: ['high'] },
    { action: 'check', elementRef: reference(obs, 'Approve fixture', 'INPUT'), checked: true },
    { action: 'click', elementRef: reference(obs, 'Increment count', 'BUTTON') },
    { action: 'click', elementRef: reference(obs, 'Increment count', 'BUTTON') },
    { action: 'click', elementRef: reference(obs, 'Suffix button', 'BUTTON') },
  ];
}
async function verifyFields(tab) {
  return until(() => {
    const state = states.get(tab.caseId);
    return state?.first === 'Synthetic first' && state.second === 'Synthetic second' && state.date === '2026-10-03' && state.priority === 'high' && state.checked && state.count === 2 && state.suffix === 1 ? state : false;
  });
}
if (process.platform !== 'win32') throw new Error('Live CEF tests require Windows');
await mkdir(profile, { recursive: true });
const pairing = { clientId: 'batch-fixture-client', token: randomBytes(32).toString('hex') };
await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1, clients: [{ id: pairing.clientId, name: 'Synthetic batch client', tokenHash: createHash('sha256').update(pairing.token).digest('hex') }], workspaces: [], accountGrants: [], operations: [] }));
await writePrivateConfig(resolve(profile, 'client.json'), { ...pairing, pipe });
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}`;
const browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { windowsHide: true, stdio: 'ignore' });
try {
  await until(alivePipe, 30_000);
  for (const era of ['modern', 'legacy']) {
    const wire = new StdioClientTransport({ command: process.execPath, args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', resolve(profile, 'client.json')], stderr: 'pipe' });
    const client = new Client({ name: `Batch ${era}`, version: '1' }, { versionNegotiation: { mode: era === 'legacy' ? 'legacy' : { pin: '2026-07-28' } } });
    await client.connect(wire); clients.push(client);
    const worker = await tool(client, 'worker_create', { name: `Batch ${era}` });
    await check(`${era}: eight ordered actions and exact-request deduplication`, async () => {
      const { tab, evidence } = await ready(client, worker, base, era);
      const args = { ...mutation(tab), observationId: evidence.observationId, steps: plan(evidence) };
      const started = performance.now(), result = await tool(client, 'batch', args), batchMs = performance.now() - started;
      assert.equal(result.status, 'completed'); assert.equal(result.steps.length, 8);
      assert(result.steps.every((step, index) => step.index === index && step.dispatchStatus === 'dispatched' && step.response.ok));
      const state = await verifyFields(tab);
      const after = await observe(client, tab); assert.equal(after.documentId, evidence.documentId);
      await tool(client, 'batch', args); await sleep(350);
      assert.equal(states.get(tab.caseId).count, 2); assert.equal(states.get(tab.caseId).suffix, 1);
      return { interactionCalls: 1, batchMs: Math.round(batchMs), steps: 8, verifiedCount: state.count, documentPreserved: true };
    });
    if (era === 'legacy') continue;
    await check('Separate calls comparison on the same eight-action plan', async () => {
      const { tab, evidence } = await ready(client, worker, base, 'serial'); const steps = plan(evidence), started = performance.now();
      for (const step of steps) await tool(client, 'interact', { ...mutation(tab), observationId: evidence.observationId, ...step });
      const serialMs = performance.now() - started; await verifyFields(tab);
      return { interactionCalls: 8, serialMs: Math.round(serialMs), steps: 8, limitation: 'Deterministic MCP harness timing; excludes model inference and does not establish a general speedup.' };
    });
    for (const [name, trigger, target, expectedError] of [
      ['Replaced target', 'Replace target', 'Original target', 'stale_element'],
      ['Covered target', 'Show overlay', 'Increment count', undefined],
    ]) await check(`${name} stops the suffix without retargeting`, async () => {
      const { tab, evidence } = await ready(client, worker, base, name);
      const result = await tool(client, 'batch', { ...mutation(tab), observationId: evidence.observationId, steps: [
        { action: 'click', elementRef: reference(evidence, trigger, 'BUTTON') },
        { action: 'click', elementRef: reference(evidence, target, 'BUTTON') },
        { action: 'click', elementRef: reference(evidence, 'Suffix button', 'BUTTON') },
      ] });
      assert.equal(result.status, 'stopped'); assert.equal(result.stoppedAt, 1);
      assert.equal(result.steps[1].response.ok, false); if (expectedError) assert.equal(result.steps[1].response.error.code, expectedError);
      assert.equal(result.steps[2].status, 'skipped'); await sleep(350);
      const state = states.get(tab.caseId); assert.equal(state.target, 0); assert.equal(state.count, 0); assert.equal(state.suffix, 0);
      const journal = await tool(client, 'operation', { operationId: result.operationId }); assert.equal(journal.state, 'failed');
      return { error: result.steps[1].response.error.code, suffixUntouched: true };
    });
    await check('Navigation cancels original-document targets', async () => {
      const { tab, evidence } = await ready(client, worker, base, 'navigation');
      const result = await tool(client, 'batch', { ...mutation(tab), observationId: evidence.observationId, steps: [
        { action: 'click', elementRef: reference(evidence, 'Navigate fixture', 'BUTTON') },
        { action: 'fill', elementRef: reference(evidence, 'First field', 'INPUT'), text: 'Must not cross navigation' },
        { action: 'click', elementRef: reference(evidence, 'Suffix button', 'BUTTON') },
      ] });
      assert.notEqual(result.status, 'completed'); assert.equal(result.steps[2].status, 'skipped');
      await until(() => destinations.has(tab.caseId));
      const after = await until(async () => { try { const obs = await observe(client, tab); return obs.nodes.some(n => n.name === 'Batch destination reached') ? obs : false; } catch { return false; } });
      assert.notEqual(after.documentId, evidence.documentId); assert.equal(states.get(tab.caseId).suffix, 0);
      return { status: result.status, stoppedAt: result.stoppedAt, destinationVerified: true };
    });
  }
} catch (error) { results.push({ name: 'Batch harness', passed: false, error: error.message }); console.log(`FAIL harness: ${error.message}`); }
finally {
  for (const client of clients) await client.close().catch(() => {});
  if (browser.pid) await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
  await new Promise(resolve => server.close(resolve));
  results.push({ name: 'Binary remained unchanged', passed: await digest() === applicationDllSha256 });
  const report = { run, capturedAt: new Date().toISOString(), binary: 'build/app/Release/Xenon.exe', applicationDllSha256, passed: results.every(r => r.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true });
  await writeFile(resolve(root, 'out', `${run}.json`), JSON.stringify(report, null, 2));
  console.log(`Report: ${resolve(root, 'out', `${run}.json`)}`); process.exitCode = report.passed ? 0 : 1;
}
