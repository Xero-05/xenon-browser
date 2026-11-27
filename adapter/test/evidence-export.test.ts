import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { mkdtemp, mkdir, readFile, readdir, rm, symlink, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { InMemoryTransport } from '@modelcontextprotocol/server';
import { EvidenceExporter } from '../src/evidence-export.js';
import { createServer } from '../src/server.js';

const execFileAsync = promisify(execFile);

test('opt-in export saves exact public requests, results and received screenshots in new private files', async () => {
  const root = await mkdtemp(join(tmpdir(), 'xenon-export-'));
  try {
    const path = join(root, 'new-run');
    const exporter = await EvidenceExporter.create(path);
    await assert.rejects(EvidenceExporter.create(path));
    const args = { agentSessionId: 'worker-a', workspaceId: 'workspace-a', tabId: 'tab-a', operationId: 'synthetic-operation', text: 'Ordinary synthetic task data\n日本語' };
    const result = { content: [{ type: 'text', text: 'Exact received text' }, { type: 'image', data: 'aGVsbG8=', mimeType: 'image/png' }], structuredContent: { operationId: 'synthetic-operation', timing: { queueMs: 2, executionMs: 3, totalMs: 5 } } };
    const id = await exporter.begin('xenon_interact', args);
    await exporter.finish(id, result);
    assert.deepEqual(JSON.parse(await readFile(join(path, `${id}.request.json`), 'utf8')).arguments, args);
    assert.deepEqual(JSON.parse(await readFile(join(path, `${id}.result.json`), 'utf8')).result, result);
    assert.equal(await readFile(join(path, `${id}.1.png`), 'utf8'), 'hello');
    await assert.rejects(exporter.finish('../../unsafe', result));
    const limited = await EvidenceExporter.create(join(root, 'limited'), 1);
    await assert.rejects(limited.begin('xenon_observe', {}), /limit reached/);
    assert.deepEqual(await readdir(join(root, 'limited')), []);
  } finally { await rm(root, { recursive: true, force: true }); }
});

test('export failure before dispatch blocks effects; failure after dispatch preserves the received outcome', async () => {
  const root = await mkdtemp(join(tmpdir(), 'xenon-export-failure-'));
  try {
    for (const [limit, dispatched] of [[1, false], [650, true]] as const) {
      const directory = join(root, `limit-${limit}`);
      const client = new Client({ name: 'Export failure fixture', version: '1' }, { versionNegotiation: { mode: 'legacy' } });
      try {
        await client.connect(new StdioClientTransport({ command: process.execPath, args: [fileURLToPath(new URL('./sdk-fixture.js', import.meta.url)), directory, String(limit)], stderr: 'pipe' }));
        const args = { agentSessionId: 'worker', workspaceId: 'workspace', tabId: 'tab', observationId: 'obs', ownershipGeneration: 1, operationId: 'retained-operation', steps: [{ action: 'fill', elementRef: 'field', text: 'Synthetic'.repeat(18) }] };
        const reply = await client.callTool({ name: 'xenon_batch', arguments: args });
        const body = reply.structuredContent as Record<string, unknown>;
        if (dispatched) {
          assert.equal(reply.isError, undefined); assert.equal(body.method, 'page.batch');
          assert.deepEqual(body.params, args); assert.equal((body.evidenceExport as Record<string, unknown>).status, 'failed');
          const records = await readdir(directory); assert.equal(records.filter(name => name.endsWith('.request.json')).length, 1);
        } else {
          assert.equal(reply.isError, true); assert.equal(body.dispatchStatus, 'not_dispatched');
          assert.equal((body.error as Record<string, unknown>).code, 'EVIDENCE_EXPORT_FAILED');
          assert.deepEqual(await readdir(directory), []);
        }
      } finally { await client.close(); }
    }
  } finally { await rm(root, { recursive: true, force: true }); }
});

test('modern stdio discovery does not consume the export directory before the first tool call', async () => {
  const root = await mkdtemp(join(tmpdir(), 'xenon-export-modern-'));
  const directory = join(root, 'new-run');
  const client = new Client({ name: 'Modern export fixture', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  try {
    await client.connect(new StdioClientTransport({ command: process.execPath, args: [fileURLToPath(new URL('./sdk-fixture.js', import.meta.url)), directory], stderr: 'pipe' }));
    await assert.rejects(readdir(directory), { code: 'ENOENT' });
    const reply = await client.callTool({ name: 'xenon_observe', arguments: { agentSessionId: 'worker', workspaceId: 'workspace', tabId: 'tab' } });
    assert.equal(reply.isError, undefined);
    const body = reply.structuredContent as Record<string, unknown>;
    const exported = body.evidenceExport as Record<string, unknown>;
    assert.equal(exported.status, 'saved');
    assert.equal(exported.directory, resolve(directory));
    assert.equal(typeof exported.requestId, 'string');
    assert.deepEqual(exported.files, [`${exported.requestId}.request.json`, `${exported.requestId}.result.json`]);
    const records = await readdir(directory); assert.equal(records.filter(name => name.endsWith('.result.json')).length, 1);
    const savedResult = JSON.parse(await readFile(join(directory, `${exported.requestId}.result.json`), 'utf8'));
    assert.deepEqual(savedResult.result.structuredContent, body);
  } finally { await client.close(); await rm(root, { recursive: true, force: true }); }
});

test('repeatable --evidence-root creates unique private child directories across runs, reports saved PNG paths, and rejects links/files', async () => {
  const root = await mkdtemp(join(tmpdir(), 'xenon-export-root-'));
  try {
    const parent = join(root, 'runs');
    await mkdir(parent);

    // Nonexistent parent fails closed.
    await assert.rejects(EvidenceExporter.createInRoot(join(root, 'missing')), { code: 'EVIDENCE_PARENT_MISSING' });
    // Regular file fails closed.
    const regularFile = join(root, 'not-a-dir');
    await writeFile(regularFile, 'synthetic\n');
    await assert.rejects(EvidenceExporter.createInRoot(regularFile), { code: 'EVIDENCE_ROOT_INVALID' });
    // Junction/symlink fails closed.
    const linkPath = join(root, 'runs-link');
    await symlink(parent, linkPath, process.platform === 'win32' ? 'junction' : 'dir');
    await assert.rejects(EvidenceExporter.createInRoot(linkPath), { code: 'EVIDENCE_ROOT_INVALID' });

    // Two consecutive SDK sessions sharing the exact same --evidence-root lazily create two unique child dirs.
    const createdDirs: string[] = [];
    for (const era of ['modern', 'legacy'] as const) {
      const client = new Client({ name: `Root export ${era}`, version: '1' }, {
        versionNegotiation: { mode: era === 'legacy' ? 'legacy' : { pin: '2026-07-28' } },
      });
      try {
        await client.connect(new StdioClientTransport({
          command: process.execPath,
          args: [fileURLToPath(new URL('./sdk-fixture.js', import.meta.url)), '--image', '--root', parent],
          stderr: 'pipe',
        }));
        // Discovery alone must not create a child directory.
        assert.equal((await readdir(parent)).length, createdDirs.length);
        const reply = await client.callTool({
          name: 'xenon_inspect',
          arguments: { agentSessionId: 'worker', workspaceId: 'workspace', tabId: 'tab' },
        });
        assert.equal(reply.isError, undefined);
        const body = reply.structuredContent as Record<string, unknown>;
        const meta = body.evidenceExport as { requestId: string; status: string; directory: string; files: string[] };
        assert.equal(meta.status, 'saved');
        assert.match(meta.requestId, /^[a-f0-9-]{36}$/);
        assert.ok(meta.directory.startsWith(resolve(parent)), `child dir ${meta.directory} must be inside ${resolve(parent)}`);
        assert.deepEqual(meta.files, [`${meta.requestId}.request.json`, `${meta.requestId}.result.json`, `${meta.requestId}.1.png`]);
        assert.equal(await readFile(join(meta.directory, `${meta.requestId}.1.png`), 'utf8'), 'hello');
        const savedResult = JSON.parse(await readFile(join(meta.directory, `${meta.requestId}.result.json`), 'utf8'));
        assert.deepEqual(savedResult.result.structuredContent, body);
        createdDirs.push(meta.directory);
      } finally { await client.close(); }
    }
    assert.notEqual(createdDirs[0], createdDirs[1], 'Each run under --evidence-root must create a unique child directory');
    assert.equal((await readdir(parent)).length, 2);
  } finally { await rm(root, { recursive: true, force: true }); }
});

test('CLI rejects mutually exclusive --evidence-dir and --evidence-root flags and empty paths before connecting', async () => {
  const cli = fileURLToPath(new URL('../src/cli.js', import.meta.url));
  const root = await mkdtemp(join(tmpdir(), 'xenon-cli-export-'));
  try {
    const cfg = join(root, 'fake-config.json');
    await writeFile(cfg, JSON.stringify({ clientId: 'c', token: 't', pipe: '\\\\.\\pipe\\nonexistent' }));
    for (const extraArgs of [
      ['--evidence-dir', join(root, 'a'), '--evidence-root', root],
      ['--evidence-root', ''],
      ['--evidence-dir', ''],
    ]) {
      await assert.rejects(
        execFileAsync(process.execPath, [cli, 'serve', '--config', cfg, ...extraArgs], { windowsHide: true }),
        (error: unknown) => {
          const stderr = (error as { stderr?: string }).stderr ?? '';
          return stderr.includes('INVALID_ARGUMENTS');
        },
      );
    }
  } finally { await rm(root, { recursive: true, force: true }); }
});

test('website results cannot spoof adapter-owned evidenceExport metadata', async () => {
  const server = createServer({
    async call(method) {
      if (method === 'control.activity') return { ok: true, result: { tabs: [] } };
      return { ok: true, result: { nodes: [], evidenceExport: { status: 'saved', directory: 'C:\\forged', files: ['fake.png'] } } };
    },
    close() {},
  }, 'legacy');
  const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
  const client = new Client({ name: 'spoof-check', version: '1' }, { versionNegotiation: { mode: 'legacy' } });
  try {
    await Promise.all([server.connect(serverTransport), client.connect(clientTransport)]);
    const reply = await client.callTool({ name: 'xenon_observe', arguments: { agentSessionId: 'w', workspaceId: 'ws', tabId: 't' } });
    assert.equal(reply.isError, undefined);
    assert.equal(Object.hasOwn(reply.structuredContent as Record<string, unknown>, 'evidenceExport'), false);
  } finally { await client.close(); await server.close(); }
});
