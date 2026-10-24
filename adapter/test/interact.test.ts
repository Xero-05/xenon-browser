import { test } from 'node:test';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';

const scope = {
  agentSessionId: 'worker', workspaceId: 'workspace', tabId: 'tab', ownershipGeneration: 1,
  observationId: 'current-evidence', operationId: 'synthetic-operation',
};

for (const era of ['legacy', 'modern'] as const) {
  test(`${era} interaction validation prevents malformed gestures reaching the broker`, async t => {
    const wire = new StdioClientTransport({
      command: process.execPath, args: [fileURLToPath(new URL('./interact-fixture.js', import.meta.url))], stderr: 'pipe',
    });
    const client = new Client({ name: 'interact-regression', version: '1.0.0' }, {
      versionNegotiation: { mode: era === 'legacy' ? 'legacy' : { pin: '2026-07-28' } },
    });
    try {
      await client.connect(wire);
      for (const [name, gesture, guidance] of [
        ['coordinate fill', { action: 'fill', x: 326, y: 299, text: 'Synthetic package' }, /fill requires elementRef/i],
        ['coordinate select', { action: 'select', x: 326, y: 299, values: ['Synthetic letter'] }, /select requires elementRef/i],
        ['coordinate check', { action: 'check', x: 326, y: 299, checked: true }, /check requires elementRef/i],
        ['full title as key', { action: 'key', key: 'Synthetic tailored letter.pdf' }, /named key|single.*letter/i],
        ['missing fill text', { action: 'fill', elementRef: 'field' }, /text/i],
        ['missing select values', { action: 'select', elementRef: 'selector' }, /values/i],
        ['empty select values', { action: 'select', elementRef: 'selector', values: [] }, /values/i],
        ['missing check state', { action: 'check', elementRef: 'checkbox' }, /checked/i],
        ['missing key', { action: 'key' }, /key/i],
      ] as const) {
        await t.test(name, async () => {
          const before = await client.callTool({ name: 'xenon_operation', arguments: { operationId: 'count' } });
          const rejected = await client.callTool({ name: 'xenon_interact', arguments: { ...scope, ...gesture } });
          const after = await client.callTool({ name: 'xenon_operation', arguments: { operationId: 'count' } });
          assert.equal((after.structuredContent as Record<string, unknown>).pageCalls, (before.structuredContent as Record<string, unknown>).pageCalls, 'Invalid gestures must not reach native dispatch');
          assert.equal(rejected.isError, true);
          assert.match(JSON.stringify(rejected.content), guidance);
        });
      }
      await t.test('key syntax rejects text, unsupported modifiers and trailing controls before dispatch', async () => {
        const before = await client.callTool({ name: 'xenon_operation', arguments: { operationId: 'count' } });
        for (const key of ['', 'ab', ' ', '+', 'Alt+Tab', 'Meta+a', 'F5', 'ArrowDown\n', 'a\r', 'a\u2028', 'é']) {
          const rejected = await client.callTool({ name: 'xenon_interact', arguments: { ...scope, action: 'key', key } });
          assert.equal(rejected.isError, true, `Unsupported key syntax: ${JSON.stringify(key)}`);
        }
        const after = await client.callTool({ name: 'xenon_operation', arguments: { operationId: 'count' } });
        assert.equal((after.structuredContent as Record<string, unknown>).pageCalls, (before.structuredContent as Record<string, unknown>).pageCalls);
      });
      await t.test('supported ref, coordinate and keyboard paths retain their arguments', async () => {
        for (const gesture of [
          { action: 'fill', elementRef: 'field', text: '' },
          { action: 'select', elementRef: 'selector', values: ['Synthetic letter'] },
          { action: 'check', elementRef: 'checkbox', checked: false },
          { action: 'click', x: 326, y: 299 },
          { action: 'hover', elementRef: 'button' },
          { action: 'scroll', direction: 'down' },
          { action: 'drag', fromX: 10, fromY: 10, toX: 20, toY: 20 },
          ...['ArrowDown', 'Space', 'a', '0', 'Ctrl+A', 'Control+Shift+ArrowLeft', 'Shift+Tab', 'Ctrl+Z', 'Ctrl+Shift+Z'].map(key => ({ action: 'key', key })),
        ]) {
          const args = { ...scope, ...gesture };
          const result = await client.callTool({ name: 'xenon_interact', arguments: args });
          assert.equal(result.isError, undefined);
          assert.deepEqual((result.structuredContent as Record<string, unknown>).params, args);
          assert.equal((result.structuredContent as Record<string, unknown>).method, `page.${gesture.action}`);
        }
      });
      await t.test('genuinely stale refs keep the native error and operation outcome', async () => {
        const result = await client.callTool({ name: 'xenon_interact', arguments: { ...scope, action: 'select', elementRef: 'native-stale', values: ['Synthetic letter'] } });
        assert.equal(result.isError, true);
        assert.deepEqual(result.structuredContent, {
          error: { code: 'stale_element', message: 'Observe the page again before using this element.' },
          operationId: scope.operationId, dispatchStatus: 'dispatched',
        });
      });
    } finally { await client.close(); }
  });
}
