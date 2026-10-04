import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, readdir, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { EvidenceExporter } from '../src/evidence-export.js';

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
    const records = await readdir(directory); assert.equal(records.filter(name => name.endsWith('.result.json')).length, 1);
  } finally { await client.close(); await rm(root, { recursive: true, force: true }); }
});
