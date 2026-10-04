import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ScopedXenonTab, createScopedWorker, XenonToolError, XenonConnectionError, type ToolClient } from '../src/scoped-client.js';

test('bound workers keep concurrent calls isolated and refuse scope overrides', async () => {
  const requests: Array<{ name: string; arguments: Record<string, unknown> }> = [];
  const client: ToolClient = { async callTool(request) { requests.push(structuredClone(request)); return { content: [], structuredContent: {} }; } };
  const a = new ScopedXenonTab(client, { agentSessionId: 'agent-a', workspaceId: 'workspace-a', tabId: 'tab-a' });
  const b = new ScopedXenonTab(client, { agentSessionId: 'agent-b', workspaceId: 'workspace-b', tabId: 'tab-b' });
  await Promise.all([a.inspect(), b.observe(), a.files({ query: 'report', limit: 1 })]);
  assert.deepEqual(requests.map(r => r.arguments), [a.scope, b.scope, { query: 'report', limit: 1, agentSessionId: 'agent-a', workspaceId: 'workspace-a' }]);
  assert.throws(() => a.call('xenon_observe', { agentSessionId: 'agent-b' }), /cannot be overridden/);
  assert.throws(() => a.call('xenon_worker_resume'), /outside/);
  assert.throws(() => new ScopedXenonTab(client, { ...a.scope, tabId: 'tab copied incorrectly' }), /opaque handle/);
  assert(Object.isFrozen(a.scope));
  assert.equal(requests.length, 3);
});

test('a lost batch reply preserves the generated operation ID without replay', async () => {
  let calls = 0, operationId: unknown;
  const tab = new ScopedXenonTab({ async callTool(request) { calls++; operationId = request.arguments.operationId; throw new Error('Disconnected'); } },
    { agentSessionId: 'worker', workspaceId: 'workspace', tabId: 'tab' });
  await assert.rejects(tab.batch([{ action: 'click', elementRef: 'button' }], { observationId: 'obs', ownershipGeneration: 1 }), error => {
    assert(error instanceof XenonConnectionError); assert.equal(error.operationId, operationId); assert.match(String(error.operationId), /^[a-f0-9-]{36}$/); return true;
  });
  assert.equal(calls, 1);
});

test('creation retains returned handles and batches never retry when follow-up evidence is denied', async () => {
  const requests: Array<{ name: string; arguments: Record<string, unknown> }> = [];
  const client: ToolClient = { async callTool(request) {
    requests.push(structuredClone(request));
    if (request.name === 'xenon_worker_create') return { content: [], structuredContent: { agentSessionId: 'worker', workspaceId: 'workspace' } };
    if (request.name === 'xenon_tab_create') return { content: [], structuredContent: { tabId: 'tab', workspaceId: 'workspace' } };
    if (request.name === 'xenon_inspect') return { content: [], isError: true, structuredContent: { error: { code: 'HUMAN_INPUT_PAUSED' } } };
    return { content: [], structuredContent: { status: 'outcome_unknown', operationId: request.arguments.operationId } };
  } };
  const worker = await createScopedWorker(client, 'Synthetic worker');
  const { tab } = await worker.createTab('https://example.test');
  const outcome = await tab.batch([{ action: 'click', elementRef: 'button' }], { ownershipGeneration: 1, observationId: 'obs' }, { inspectAfter: true });
  assert.equal(outcome.batch.structuredContent?.status, 'outcome_unknown');
  assert.equal(outcome.inspection?.isError, true);
  assert.equal(requests.filter(r => r.name === 'xenon_batch').length, 1);
  assert.match(String(outcome.batch.structuredContent?.operationId), /^[a-f0-9-]{36}$/);
  await assert.rejects(tab.inspect(), XenonToolError);
});
