// Verify the actual release directory with its bundled Node and MCP adapter.
// Uses only a generated profile and synthetic pairing, then stops its own PID.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve, basename } from 'node:path';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';
import { verifyBranding } from './branding-resources.mjs';

assert(process.argv[2], 'Pass the extracted release directory.');
const root = resolve(import.meta.dirname, '..'), release = resolve(process.argv[2]);
const run = `package-smoke-${Date.now()}`, profile = resolve(root, '.cache', run);
const pipeName = `xenon-test-${run}`, pipePath = `\\\\.\\pipe\\${pipeName}`;
const runFile = promisify(execFile), results = [];
const hash = async path => createHash('sha256').update(await readFile(path)).digest('hex');
const manifest = JSON.parse(await readFile(resolve(release, 'release-manifest.json'), 'utf8'));
const applicationDllSha256 = await hash(resolve(release, 'Xenon.dll'));
assert.equal(applicationDllSha256, manifest.files.find(f => f.path === 'Xenon.dll')?.sha256);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const pipeAlive = () => new Promise(r => {
  const s = net.connect(pipePath);
  s.once('connect', () => { s.destroy(); r(true); });
  s.once('error', () => { s.destroy(); r(false); });
});
async function until(fn, timeout = 15000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) { const result = await fn(); if (result) return result; await sleep(100); }
  throw new Error('Release smoke test timed out.');
}
async function check(name, fn) {
  const start = Date.now();
  try { await fn(); results.push({ name, passed: true, elapsedMs: Date.now() - start }); console.log(`PASS ${name}`); }
  catch (error) { results.push({ name, passed: false, error: error.message }); throw error; }
}
assert(!(await pipeAlive()), 'The generated package test pipe is already in use. Start a fresh test run.');
await mkdir(profile, { recursive: true });
const config = { clientId: 'package_fixture', token: randomBytes(32).toString('hex'), pipe: pipePath };
const configPath = resolve(profile, 'client-config.json');
await writePrivateConfig(configPath, config);
await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
  clients: [{ id: config.clientId, name: 'Release smoke fixture', tokenHash: createHash('sha256').update(config.token).digest('hex') }],
  workspaces: [], accountGrants: [], operations: [] }));
const server = http.createServer((req, res) => {
  res.setHeader('Content-Type', 'text/html; charset=utf-8');
  res.end('<!doctype html><title>Xenon release fixture</title><style>body{font:16px system-ui;margin:18px;background:#f2f4f8;color:#18212d}label{display:block;margin:12px 0}input{margin:4px}</style><h1>Packaged browser ready</h1><button onclick="this.textContent=\'Package action verified\'">Verify package action</button><label>Package first field <input id="first"></label><label>Package second field <input id="second"></label><button onclick="if(document.querySelector(\'#first\').value===\'Synthetic first\'&&document.querySelector(\'#second\').value===\'Synthetic second\')this.textContent=\'Package batch verified\'">Verify package batch</button>');
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
let browser, client, tab, branding;
try {
  await check('Packaged branding changes only permitted CEF bootstrap resources', async () => {
    branding = await verifyBranding({ release }); assert.equal(branding.passed, true);
    assert.equal(branding.applicationDllSha256, applicationDllSha256);
    assert.equal(branding.executableSha256, manifest.files.find(file => file.path === 'Xenon.exe')?.sha256);
  });
  browser = spawn(resolve(release, 'Xenon.exe'), [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { windowsHide: true, stdio: 'ignore' });
  await check('Packaged CEF browser and bundled Node MCP adapter connect', async () => {
    await until(pipeAlive, 30000);
    client = new Client({ name: 'Xenon release smoke', version: '1' });
    await client.connect(new StdioClientTransport({ command: resolve(release, 'runtime/node.exe'),
      args: [resolve(release, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath], stderr: 'pipe' }));
    assert((await client.listTools()).tools.some(tool => tool.name === 'xenon_batch'), 'Packaged adapter must expose batching');
  });
  const tool = async (name, args = {}) => {
    const response = await client.callTool({ name: `xenon_${name}`, arguments: args });
    assert(!response.isError, `Release tool failed: ${name}`);
    return response.structuredContent;
  };
  const observe = async args => {
    const deadline = Date.now() + 5000;
    while (true) {
      const response = await client.callTool({ name: 'xenon_observe', arguments: args });
      // Retry only the documented transient read, never a mutation.
      if (response.isError && response.structuredContent?.error?.code === 'privacy_guard_initializing' && Date.now() < deadline) {
        await sleep(100); continue;
      }
      assert(!response.isError, `Release observation failed: ${JSON.stringify(response.structuredContent)}`);
      return response.structuredContent;
    }
  };
  await check('Packaged browser observes and performs a verified page action', async () => {
    const worker = await tool('worker_create', { name: 'Release smoke worker' });
    tab = { ...worker, ...await tool('tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: `http://127.0.0.1:${server.address().port}/` }) };
    const scope = { agentSessionId: tab.agentSessionId, workspaceId: tab.workspaceId, tabId: tab.tabId };
    const evidence = await until(async () => {
      const observation = await observe(scope);
      return observation.nodes?.some(n => n.role === 'button' && n.name === 'Verify package action') ? observation : false;
    });
    const button = evidence.nodes.find(n => n.role === 'button' && n.name === 'Verify package action');
    await tool('interact', { ...scope, ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(),
      observationId: evidence.observationId, action: 'click', elementRef: button.ref });
    await until(async () => (await observe(scope)).nodes?.some(n => n.role === 'button' && n.name === 'Package action verified'));
  });
  await check('Packaged batch fills multiple fields and verifies website state', async () => {
    const scope = { agentSessionId: tab.agentSessionId, workspaceId: tab.workspaceId, tabId: tab.tabId };
    const evidence = await observe(scope);
    const ref = (name, tag) => {
      const node = evidence.nodes.find(node => node.name === name && node.tag === tag && node.ref);
      assert(node, `Missing packaged batch target: ${name}; rendered targets: ${JSON.stringify(evidence.nodes.map(node => ({ name: node.name, tag: node.tag })))}`); return node.ref;
    };
    const result = await tool('batch', { ...scope, ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID(), observationId: evidence.observationId,
      steps: [{ action: 'fill', elementRef: ref('Package first field', 'INPUT'), text: 'Synthetic first' },
        { action: 'fill', elementRef: ref('Package second field', 'INPUT'), text: 'Synthetic second' },
        { action: 'click', elementRef: ref('Verify package batch', 'BUTTON') }] });
    assert.equal(result.status, 'completed'); assert.equal(result.steps.length, 3);
    await until(async () => (await observe(scope)).nodes?.some(node => node.role === 'button' && node.name === 'Package batch verified'));
  });
} catch (error) {
  if (!results.some(r => !r.passed)) results.push({ name: 'Release harness', passed: false, error: error.message });
  console.log(`FAIL ${error.message}`);
} finally {
  await client?.close().catch(() => {});
  if (browser?.pid) await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
  server.closeAllConnections(); await new Promise(r => server.close(r));
  const report = { run, capturedAt: new Date().toISOString(), release: basename(release),
    applicationDllSha256, branding, passed: results.length === 4 && results.every(r => r.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true });
  await writeFile(resolve(root, 'out/package-smoke-results.json'), JSON.stringify(report, null, 2) + '\n');
  process.exitCode = report.passed ? 0 : 1;
}
