import { test } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import { randomUUID } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { InMemoryTransport } from '@modelcontextprotocol/server';
import { BrokerError, PipeTransport, type BrokerEvent, type BrokerTransport, type NativeReply } from '../src/ipc.js';
import { ACTIVITY_URI, HumanActivityMonitor, type HumanActivityTab } from '../src/human-activity.js';
import { createServer } from '../src/server.js';

const testPipePath = (prefix: string) =>
  process.platform === 'win32' ? `\\\\.\\pipe\\${prefix}-${randomUUID()}` : `/tmp/${prefix}-${randomUUID()}.sock`;

const baseActivityTab = (overrides: Partial<HumanActivityTab> = {}): HumanActivityTab => ({
  tabId: 'tab-1',
  workspaceId: 'workspace-1',
  ownerSessionId: 'worker-1',
  ownershipGeneration: 2,
  humanActivityEpoch: 0,
  humanPaused: false,
  humanPauseUntil: null,
  requiresFreshObservation: false,
  ...overrides,
});

test('PipeTransport delivers fragmented unsolicited push events interleaved with out-of-order RPC responses', async () => {
  const pipe = testPipePath('xenon-push-test');
  const requests: Array<{ id: string; method: string; params: Record<string, unknown> }> = [];
  const peers = new Set<net.Socket>();
  const initialPush = { event: 'control.activity', tabs: [baseActivityTab({ humanActivityEpoch: 1, humanPaused: true })] };
  const midPush = { event: 'control.activity', tabs: [baseActivityTab({ humanActivityEpoch: 2, humanHelpHold: true, attentionRequested: true, attentionKind: 'help' })] };
  const latePush = { event: 'control.activity', tabs: [baseActivityTab({ humanActivityEpoch: 3, humanHelpHold: false, attentionResolvedEpoch: 1 })] };
  const afterUnsubPush = { event: 'control.activity', tabs: [baseActivityTab({ humanActivityEpoch: 4 })] };
  let interleavedSent = false;

  const listener = net.createServer(socket => {
    peers.add(socket);
    socket.on('error', () => {});
    let buffer = '';
    socket.on('data', data => {
      buffer += data.toString('utf8');
      while (buffer.includes('\n')) {
        const at = buffer.indexOf('\n');
        const request = JSON.parse(buffer.slice(0, at)) as typeof requests[number];
        buffer = buffer.slice(at + 1);
        requests.push(request);

        if (request.method === 'hello') {
          socket.write(JSON.stringify({ id: request.id, ok: true, result: {} }) + '\n' + JSON.stringify(initialPush) + '\n');
          continue;
        }
        if (request.method === 'third') {
          socket.write(JSON.stringify(afterUnsubPush) + '\n' + JSON.stringify({ id: request.id, ok: true, result: { step: 'third' } }) + '\n');
          continue;
        }
        if (!interleavedSent) {
          const first = requests.find(r => r.method === 'first');
          const second = requests.find(r => r.method === 'second');
          if (first && second) {
            interleavedSent = true;
            const frame1 = JSON.stringify(midPush) + '\n';
            const reply2 = JSON.stringify({ id: second.id, ok: true, result: { tabId: 'tab-2' } }) + '\n';
            const frame2 = JSON.stringify(latePush) + '\n';
            const reply1 = JSON.stringify({ id: first.id, ok: true, result: { tabId: 'tab-1' } }) + '\n';
            const combined = frame1 + reply2 + frame2 + reply1;
            const cuts = [
              11,
              Math.min(frame1.length - 3, 47),
              frame1.length + 14,
              frame1.length + reply2.length + 19,
              frame1.length + reply2.length + frame2.length + 7,
            ].filter((cut, idx, arr) => cut > 0 && cut < combined.length && (idx === 0 || cut > arr[idx - 1]!));
            const chunks: string[] = [];
            let offset = 0;
            for (const cut of cuts) {
              chunks.push(combined.slice(offset, cut));
              offset = cut;
            }
            chunks.push(combined.slice(offset));
            chunks.forEach((chunk, index) => {
              setTimeout(() => { if (!socket.destroyed) socket.write(chunk); }, index * 5);
            });
          }
        }
      }
    });
  });

  await new Promise<void>((resolve, reject) => { listener.once('error', reject); listener.listen(pipe, resolve); });
  const connection = new PipeTransport(pipe);
  const eventsPrimary: BrokerEvent[] = [];
  const eventsSecondary: BrokerEvent[] = [];
  const unsubPrimary = connection.onEvent(event => { eventsPrimary.push(event); });
  // A throwing listener must never disrupt other listeners or RPC transport.
  const unsubThrowing = connection.onEvent(() => { throw new Error('listener boom'); });
  const unsubSecondary = connection.onEvent(event => { eventsSecondary.push(event); });

  try {
    await connection.connect({ clientId: 'push-client', token: 'push-token' });
    const [first, second] = await Promise.all([connection.call('first'), connection.call('second')]);
    assert.equal(first.ok, true);
    assert.deepEqual(first.result, { tabId: 'tab-1' });
    assert.equal(second.ok, true);
    assert.deepEqual(second.result, { tabId: 'tab-2' });
    assert.deepEqual(eventsPrimary, [initialPush, midPush, latePush]);
    assert.deepEqual(eventsSecondary, [initialPush, midPush, latePush]);

    unsubPrimary();
    unsubThrowing();
    const third = await connection.call('third');
    assert.equal(third.ok, true);
    assert.deepEqual(third.result, { step: 'third' });
    assert.deepEqual(eventsPrimary, [initialPush, midPush, latePush], 'unsubscribed listener must not receive subsequent push events');
    assert.deepEqual(eventsSecondary, [initialPush, midPush, latePush, afterUnsubPush]);
    unsubSecondary();
  } finally {
    connection.close();
    for (const peer of peers) peer.destroy();
    await new Promise<void>(resolve => listener.close(() => resolve()));
  }
});

test('PipeTransport rejects malformed frames lacking event or id/ok with INVALID_RESPONSE and closes the transport', async t => {
  const malformedFrames: Array<[string, string]> = [
    ['object without event or id/ok', JSON.stringify({ unexpected: true })],
    ['non-string event without id/ok', JSON.stringify({ event: 123, tabs: [] })],
    ['both id and event without boolean ok', JSON.stringify({ id: 'req-1', event: 'control.activity' })],
    ['response missing ok boolean', JSON.stringify({ id: 'req-1', result: {} })],
  ];

  for (const [label, badFrame] of malformedFrames) {
    await t.test(label, async () => {
      const pipe = testPipePath('xenon-push-test');
      const peers = new Set<net.Socket>();
      const listener = net.createServer(socket => {
        peers.add(socket);
        socket.on('error', () => {});
        let buffer = '';
        socket.on('data', data => {
          buffer += data.toString('utf8');
          while (buffer.includes('\n')) {
            const at = buffer.indexOf('\n');
            const request = JSON.parse(buffer.slice(0, at)) as { id: string; method: string };
            buffer = buffer.slice(at + 1);
            if (request.method === 'hello') {
              socket.write(JSON.stringify({ id: request.id, ok: true, result: {} }) + '\n');
            } else {
              socket.write(badFrame + '\n');
            }
          }
        });
      });

      await new Promise<void>((resolve, reject) => { listener.once('error', reject); listener.listen(pipe, resolve); });
      const connection = new PipeTransport(pipe);
      const events: BrokerEvent[] = [];
      connection.onEvent(event => { events.push(event); });
      try {
        await connection.connect({ clientId: 'push-client', token: 'push-token' });
        await assert.rejects(
          connection.call('control.status', {}),
          error => error instanceof BrokerError && error.code === 'INVALID_RESPONSE',
        );
        assert.deepEqual(events, []);
        await assert.rejects(
          connection.call('control.status', {}),
          error => error instanceof BrokerError && error.code === 'DISCONNECTED',
        );
      } finally {
        connection.close();
        for (const peer of peers) peer.destroy();
        await new Promise<void>(resolve => listener.close(() => resolve()));
      }
    });
  }
});

test('HumanActivityMonitor handles push-driven control.activity updates without polling, preserves allowlisted fields, strips canaries, and stops cleanly', async () => {
  const listeners = new Set<(event: BrokerEvent) => void>();
  let capturedListener: ((event: BrokerEvent) => void) | undefined;
  let unsubscribed = false;
  let pollCalls = 0;
  let notifications = 0;

  const transport: BrokerTransport = {
    async call(method) {
      pollCalls++;
      assert.fail(`Unexpected transport.call(${method}) while push-driven monitor is active`);
    },
    onEvent(listener) {
      capturedListener = listener;
      listeners.add(listener);
      return () => {
        unsubscribed = true;
        listeners.delete(listener);
      };
    },
    close() {},
  };

  const emit = (event: BrokerEvent) => {
    for (const listener of [...listeners]) listener(event);
  };

  const monitor = new HumanActivityMonitor(transport, async () => { notifications++; });
  try {
    monitor.start();
    assert.equal(listeners.size, 1);
    assert.equal(pollCalls, 0);

    // Baseline state via push (no transition changes yet).
    emit({
      event: 'control.activity',
      tabs: [baseActivityTab({ humanHelpHold: false, attentionRequested: false, attentionResolvedEpoch: 0 })],
    });
    assert.equal(pollCalls, 0);
    assert.equal(notifications, 0);
    assert.equal(monitor.snapshot().available, true);
    assert.equal(monitor.snapshot().tabs.length, 1);

    // Push transition into human help hold with canary fields that must be stripped.
    emit({
      event: 'control.activity',
      tabs: [{
        ...baseActivityTab({
          humanPaused: true,
          humanPauseUntil: null,
          requiresFreshObservation: true,
          humanActivityEpoch: 1,
          humanHelpHold: true,
          attentionRequested: true,
          attentionKind: 'help',
          attentionResolvedEpoch: 0,
        }),
        password: 'SECRET_PASSWORD_CANARY',
        url: 'https://secret.example/account',
        attentionReason: 'SECRET_NOTE',
        title: 'SECRET_PAGE_TITLE',
      }],
    });

    // Immediate notification callback with zero wait and zero poll RPCs.
    assert.equal(notifications, 1);
    assert.equal(pollCalls, 0);
    const heldSnapshot = monitor.snapshot();
    const heldTab = heldSnapshot.tabs[0];
    assert.ok(heldTab);
    assert.equal(heldTab.humanHelpHold, true);
    assert.equal(heldTab.attentionRequested, true);
    assert.equal(heldTab.attentionKind, 'help');
    assert.equal(heldTab.attentionResolvedEpoch, 0);
    assert.equal(Object.hasOwn(heldTab, 'password'), false);
    assert.equal(Object.hasOwn(heldTab, 'url'), false);
    assert.equal(Object.hasOwn(heldTab, 'attentionReason'), false);
    assert.equal(Object.hasOwn(heldTab, 'title'), false);
    const heldSerialized = JSON.stringify(heldSnapshot);
    assert.ok(!heldSerialized.includes('SECRET_PASSWORD_CANARY'));
    assert.ok(!heldSerialized.includes('secret.example'));
    assert.ok(!heldSerialized.includes('SECRET_NOTE'));
    assert.ok(!heldSerialized.includes('SECRET_PAGE_TITLE'));
    assert.ok(heldSnapshot.changes.some(c => c.tabId === 'tab-1' && c.changes.includes('help_hold_changed')));

    // Unrelated push event must be ignored.
    emit({ event: 'control.other', tabs: [] });
    assert.equal(notifications, 1);
    assert.equal(monitor.snapshot().tabs.length, 1);

    // Push help resolution + review attention kind + invalid optional field values on a second tab.
    emit({
      event: 'control.activity',
      tabs: [
        {
          ...baseActivityTab({
            humanPaused: false,
            humanPauseUntil: null,
            requiresFreshObservation: true,
            humanActivityEpoch: 2,
            humanHelpHold: false,
            attentionRequested: false,
            attentionKind: 'review',
            attentionResolvedEpoch: 1,
          }),
          attentionReason: 'SECRET_NOTE_AFTER_RESOLVE',
          password: 'SECRET_PASSWORD_2',
        },
        {
          ...baseActivityTab({ tabId: 'tab-2' }),
          humanHelpHold: 'not-a-boolean',
          attentionRequested: 1,
          attentionKind: 'urgent',
          attentionResolvedEpoch: -5,
          attentionReason: 'SECRET_NOTE_TAB_2',
        },
      ],
    });

    assert.equal(notifications, 2);
    assert.equal(pollCalls, 0);
    const resolvedSnapshot = monitor.snapshot();
    const resolvedTab = resolvedSnapshot.tabs.find(tab => tab.tabId === 'tab-1');
    const secondTab = resolvedSnapshot.tabs.find(tab => tab.tabId === 'tab-2');
    assert.ok(resolvedTab);
    assert.equal(resolvedTab.humanHelpHold, false);
    assert.equal(resolvedTab.attentionRequested, false);
    assert.equal(resolvedTab.attentionKind, 'review');
    assert.equal(resolvedTab.attentionResolvedEpoch, 1);
    assert.ok(secondTab);
    assert.equal(Object.hasOwn(secondTab, 'humanHelpHold'), false);
    assert.equal(Object.hasOwn(secondTab, 'attentionRequested'), false);
    assert.equal(Object.hasOwn(secondTab, 'attentionKind'), false);
    assert.equal(Object.hasOwn(secondTab, 'attentionResolvedEpoch'), false);
    assert.ok(!JSON.stringify(resolvedSnapshot).includes('SECRET_'));

    const tab1Changes = resolvedSnapshot.changes.find(c => c.tabId === 'tab-1');
    assert.ok(tab1Changes);
    assert.ok(tab1Changes.changes.includes('help_hold_changed'));
    assert.ok(tab1Changes.changes.includes('help_resolved'));

    const notice = monitor.takeNotice();
    assert.ok(notice);
    assert.ok(notice.changes.some(c => c.tabId === 'tab-1' && c.changes.includes('help_hold_changed') && c.changes.includes('help_resolved')));
    assert.deepEqual(monitor.snapshot().changes, []);

    // Stop unsubscribes and ignores subsequent pushes (even if the captured listener is invoked directly).
    monitor.stop();
    assert.equal(unsubscribed, true);
    assert.equal(listeners.size, 0);
    emit({
      event: 'control.activity',
      tabs: [baseActivityTab({ humanPaused: true, humanActivityEpoch: 99, humanHelpHold: true, attentionResolvedEpoch: 5 })],
    });
    capturedListener?.({
      event: 'control.activity',
      tabs: [baseActivityTab({ humanPaused: true, humanActivityEpoch: 100, humanHelpHold: true, attentionResolvedEpoch: 6 })],
    });
    assert.equal(notifications, 2);
    assert.equal(pollCalls, 0);
    assert.deepEqual(monitor.snapshot().tabs, []);
    assert.deepEqual(monitor.snapshot().changes, []);
  } finally {
    monitor.stop();
  }
});

const scope = { agentSessionId: 'worker', workspaceId: 'workspace', tabId: 'tab' };
const mutation = { ...scope, ownershipGeneration: 1, operationId: 'op-1' };

for (const era of ['legacy', 'modern'] as const) {
  test(`real ${era} SDK stdio enforces functional-improvement schemas for new and extended tools`, async t => {
    const fixture = fileURLToPath(new URL('./sdk-fixture.js', import.meta.url));
    const wire = new StdioClientTransport({ command: process.execPath, args: [fixture], stderr: 'pipe' });
    const client = new Client({ name: 'xenon-functional-improvements-test', version: '1.0.0' }, {
      versionNegotiation: { mode: era === 'legacy' ? 'legacy' : { pin: '2026-07-28' } },
    });

    try {
      await client.connect(wire);
      const listed = await client.listTools();

      await t.test('xenon_wait_ready annotations, forwarding, and schema bounds', async () => {
        const waitReadyTool = listed.tools.find(tool => tool.name === 'xenon_wait_ready');
        assert.ok(waitReadyTool);
        assert.equal(waitReadyTool.annotations?.readOnlyHint, true);
        assert.equal(waitReadyTool.annotations?.destructiveHint, false);
        assert.equal(waitReadyTool.annotations?.idempotentHint, true);
        assert.equal(waitReadyTool.inputSchema.additionalProperties, false);
        assert.deepEqual([...(waitReadyTool.inputSchema.required ?? [])].sort(), ['agentSessionId', 'tabId', 'workspaceId']);

        for (const args of [
          scope,
          { ...scope, timeoutMs: 5000, waitForHelpResolution: true },
          { ...scope, timeoutMs: 100, waitForHelpResolution: false },
          { ...scope, timeoutMs: 30_000 },
        ]) {
          const reply = await client.callTool({ name: 'xenon_wait_ready', arguments: args });
          assert.equal(reply.isError, undefined, `xenon_wait_ready should accept ${JSON.stringify(args)}`);
          const body = reply.structuredContent as Record<string, unknown>;
          assert.deepEqual(body, { method: 'control.wait_ready', params: args });
          assert.equal(Object.hasOwn(body, 'contentTrust'), false);
        }

        for (const badArgs of [
          { ...scope, timeoutMs: 50 },
          { ...scope, timeoutMs: 99 },
          { ...scope, timeoutMs: 30_001 },
          { ...scope, timeoutMs: 40_000 },
          { ...scope, timeoutMs: 250.5 },
          { ...scope, waitForHelpResolution: 'true' },
          { ...scope, unexpectedExtra: true },
          { agentSessionId: 'worker', workspaceId: 'workspace' },
        ]) {
          const rejected = await client.callTool({ name: 'xenon_wait_ready', arguments: badArgs });
          assert.equal(rejected.isError, true, `xenon_wait_ready must reject ${JSON.stringify(badArgs)}`);
        }
      });

      await t.test('xenon_observe and xenon_inspect compact, roles, interactiveOnly, and sinceObservationId', async () => {
        const max32Roles = Array.from({ length: 32 }, (_, idx) => `role_${idx}`);
        const observeValid = [
          { ...scope, compact: true, roles: ['button', 'link', 'text_box', 'aria-live'], interactiveOnly: true, sinceObservationId: 'obs-prev-1' },
          { ...scope, roles: max32Roles, compact: false, interactiveOnly: false },
        ];
        for (const args of observeValid) {
          const reply = await client.callTool({ name: 'xenon_observe', arguments: args });
          assert.equal(reply.isError, undefined, `xenon_observe should accept ${JSON.stringify(args)}`);
          const body = reply.structuredContent as Record<string, unknown>;
          assert.equal(body.method, 'page.observe');
          assert.deepEqual(body.params, args);
        }

        const inspectValid = [
          { ...scope, compact: true, roles: ['button', 'textbox'], interactiveOnly: true, sinceObservationId: 'obs-prev-1' },
          { ...scope, roles: max32Roles },
        ];
        for (const args of inspectValid) {
          const reply = await client.callTool({ name: 'xenon_inspect', arguments: args });
          assert.equal(reply.isError, undefined, `xenon_inspect should accept ${JSON.stringify(args)}`);
          const body = reply.structuredContent as Record<string, unknown>;
          assert.equal(body.method, 'page.inspect');
          assert.deepEqual(body.params, args);
          if ('compact' in args && args.compact === true) {
            assert.deepEqual(body.textSafety, { withheldValues: 0, limitation: 'unsafe_non_rendering_withheld' });
          } else {
            assert.match(String((body.textSafety as Record<string, unknown>).limitation), /not a complete prompt-injection defense/);
          }
        }

        const sharedInvalid = [
          { ...scope, roles: [] },
          { ...scope, roles: [''] },
          { ...scope, roles: ['button link'] },
          { ...scope, roles: ['button/link'] },
          { ...scope, roles: ['btn.primary'] },
          { ...scope, roles: ['button;drop'] },
          { ...scope, roles: ['rôle'] },
          { ...scope, roles: ['a'.repeat(65)] },
          { ...scope, roles: Array.from({ length: 33 }, (_, idx) => `role_${idx}`) },
          { ...scope, compact: 'true' },
          { ...scope, interactiveOnly: 1 },
          { ...scope, sinceObservationId: '' },
          { ...scope, sinceObservationId: 'bad obs id' },
        ];
        for (const toolName of ['xenon_observe', 'xenon_inspect'] as const) {
          for (const badArgs of sharedInvalid) {
            const rejected = await client.callTool({ name: toolName, arguments: badArgs });
            assert.equal(rejected.isError, true, `${toolName} must reject ${JSON.stringify(badArgs)}`);
          }
        }
      });

      await t.test('xenon_wait anyOf, absent, returnMode, compact, and validation bounds', async () => {
        const validCases = [
          { ...scope, anyOf: ['Saved', 'Error'] },
          { ...scope, anyOf: ['Loading...'], absent: true, returnMode: 'none', compact: true },
          { ...scope, text: 'Submitted', returnMode: 'matching', compact: true },
          { ...scope, text: 'Submitted', returnMode: 'full', absent: false },
          { ...scope, text: 'Done', anyOf: ['Complete', 'Finished'], returnMode: 'matching' },
          { ...scope, anyOf: Array.from({ length: 16 }, (_, idx) => `option-${idx}`) },
        ];
        for (const args of validCases) {
          const reply = await client.callTool({ name: 'xenon_wait', arguments: args });
          assert.equal(reply.isError, undefined, `xenon_wait should accept ${JSON.stringify(args)}`);
          const body = reply.structuredContent as Record<string, unknown>;
          assert.equal(body.method, 'page.wait');
          assert.deepEqual(body.params, args);
        }

        const invalidCases = [
          scope,
          { ...scope, absent: true, returnMode: 'none', compact: true },
          { ...scope, anyOf: [] },
          { ...scope, anyOf: [''] },
          { ...scope, anyOf: Array.from({ length: 17 }, (_, idx) => `option-${idx}`) },
          { ...scope, text: 'Done', returnMode: 'minimal' },
          { ...scope, text: 'Done', absent: 'true' },
          { ...scope, text: 'Done', compact: 1 },
        ];
        for (const badArgs of invalidCases) {
          const rejected = await client.callTool({ name: 'xenon_wait', arguments: badArgs });
          assert.equal(rejected.isError, true, `xenon_wait must reject ${JSON.stringify(badArgs)}`);
        }
      });

      await t.test('xenon_screenshot clip or candidateId exclusivity and geometry bounds', async () => {
        for (const args of [
          scope,
          { ...scope, clip: { x: 0, y: 12.5, width: 320, height: 240 } },
          { ...scope, candidateId: 'cand-42' },
        ]) {
          const reply = await client.callTool({ name: 'xenon_screenshot', arguments: args });
          assert.equal(reply.isError, undefined, `xenon_screenshot should accept ${JSON.stringify(args)}`);
          const body = reply.structuredContent as Record<string, unknown>;
          assert.equal(body.method, 'page.screenshot');
          assert.deepEqual(body.params, args);
        }

        for (const badArgs of [
          { ...scope, clip: { x: 10, y: 20, width: 100, height: 50 }, candidateId: 'cand-42' },
          { ...scope, clip: { x: 0, y: 0, width: 0, height: 50 } },
          { ...scope, clip: { x: 0, y: 0, width: 50, height: 0 } },
          { ...scope, clip: { x: 0, y: 0, width: -10, height: 50 } },
          { ...scope, clip: { x: 0, y: 0, width: 50, height: -1 } },
          { ...scope, clip: { x: -1, y: 0, width: 50, height: 50 } },
          { ...scope, clip: { x: 0, y: -5, width: 50, height: 50 } },
          { ...scope, clip: { x: 0, y: 0, width: 50, height: 50, scale: 2 } },
          { ...scope, candidateId: '' },
          { ...scope, candidateId: 'bad candidate id' },
        ]) {
          const rejected = await client.callTool({ name: 'xenon_screenshot', arguments: badArgs });
          assert.equal(rejected.isError, true, `xenon_screenshot must reject ${JSON.stringify(badArgs)}`);
        }
      });

      await t.test('xenon_batch hover, key, scroll steps and observeAfter, plus rejection of invalid shapes', async () => {
        const batchArgs = {
          ...mutation,
          observationId: 'obs-1',
          steps: [
            { action: 'fill', elementRef: 'input-1', text: 'Synthetic value' },
            { action: 'hover', elementRef: 'btn' },
            { action: 'click', elementRef: 'btn' },
            { action: 'key', key: 'Enter' },
            { action: 'key', key: 'Shift+Tab' },
            { action: 'scroll', direction: 'down', amount: 600 },
            { action: 'scroll', direction: 'up' },
          ],
          observeAfter: { compact: true, maxNodes: 200, interactiveOnly: true, settleMs: 150 },
        };
        const reply = await client.callTool({ name: 'xenon_batch', arguments: batchArgs });
        assert.equal(reply.isError, undefined);
        const body = reply.structuredContent as Record<string, unknown>;
        assert.equal(body.method, 'page.batch');
        assert.deepEqual(body.params, batchArgs);

        for (const badSteps of [
          [{ action: 'key', elementRef: 'btn', key: 'Enter' }],
          [{ action: 'key', key: 'hello world' }],
          [{ action: 'key', key: 'Alt+Tab' }],
          [{ action: 'key', key: 'Enter\n' }],
          [{ action: 'key', key: '' }],
          [{ action: 'hover', x: 10, y: 20 }],
          [{ action: 'hover' }],
          [{ action: 'hover', elementRef: 'btn', extra: true }],
          [{ action: 'scroll' }],
          [{ action: 'scroll', direction: 'diagonal' }],
          [{ action: 'scroll', direction: 'down', amount: 0 }],
          [{ action: 'scroll', direction: 'down', amount: 10_001 }],
          [{ action: 'scroll', direction: 'down', elementRef: 'btn' }],
          [{ action: 'type', text: 'must stay single-action' }],
        ]) {
          const rejected = await client.callTool({
            name: 'xenon_batch',
            arguments: { ...mutation, observationId: 'obs-1', steps: badSteps },
          });
          assert.equal(rejected.isError, true, `xenon_batch must reject steps=${JSON.stringify(badSteps)}`);
        }

        for (const badObserveAfter of [
          { screenshot: true },
          { settleMs: -1 },
          { settleMs: 2001 },
          { maxNodes: 10 },
          { roles: [] },
          { tabId: 'override-tab' },
          { sinceObservationId: 'bad id' },
        ]) {
          const rejectedBatch = await client.callTool({
            name: 'xenon_batch',
            arguments: { ...mutation, observationId: 'obs-1', steps: [{ action: 'scroll', direction: 'down' }], observeAfter: badObserveAfter },
          });
          assert.equal(rejectedBatch.isError, true, `xenon_batch must reject observeAfter=${JSON.stringify(badObserveAfter)}`);
          const rejectedInteract = await client.callTool({
            name: 'xenon_interact',
            arguments: { ...mutation, observationId: 'obs-1', action: 'scroll', direction: 'down', observeAfter: badObserveAfter },
          });
          assert.equal(rejectedInteract.isError, true, `xenon_interact must reject observeAfter=${JSON.stringify(badObserveAfter)}`);
        }

        // xenon_interact accepts observeAfter across actions including scroll and type.
        for (const args of [
          { ...mutation, observationId: 'obs-1', action: 'scroll', direction: 'down', amount: 500, observeAfter: {} },
          { ...mutation, observationId: 'obs-1', action: 'click', elementRef: 'btn', observeAfter: { query: 'Ready', roles: ['button'], compact: false, sinceObservationId: 'obs-1', settleMs: 0 } },
          { ...mutation, observationId: 'obs-1', action: 'type', text: 'Focused text', observeAfter: { compact: true, maxNodes: 100 } },
        ]) {
          const ok = await client.callTool({ name: 'xenon_interact', arguments: args });
          assert.equal(ok.isError, undefined, `xenon_interact should accept ${JSON.stringify(args)}`);
          assert.deepEqual((ok.structuredContent as Record<string, unknown>).params, args);
        }

        // xenon_operation accepts optional agentSessionId for per-worker disambiguation.
        for (const args of [
          { operationId: 'op-shared-1' },
          { operationId: 'op-shared-1', agentSessionId: 'worker-a' },
        ]) {
          const ok = await client.callTool({ name: 'xenon_operation', arguments: args });
          assert.equal(ok.isError, undefined);
          assert.deepEqual((ok.structuredContent as Record<string, unknown>).params, args);
        }
        for (const badOp of [
          { operationId: 'op-shared-1', agentSessionId: '' },
          { operationId: 'op-shared-1', agentSessionId: 'bad worker' },
          { operationId: 'op-shared-1', workspaceId: 'unexpected' },
        ]) {
          const bad = await client.callTool({ name: 'xenon_operation', arguments: badOp });
          assert.equal(bad.isError, true);
        }
      });

      await t.test('xenon_attention kind help and review acceptance and invalid kind rejection', async () => {
        for (const kind of ['help', 'review'] as const) {
          const args = { ...scope, ownershipGeneration: 2, action: 'request', kind, reason: `Needs ${kind}` };
          const reply = await client.callTool({ name: 'xenon_attention', arguments: args });
          assert.equal(reply.isError, undefined, `xenon_attention should accept kind=${kind}`);
          assert.deepEqual(reply.structuredContent, { method: 'attention.request', params: args });
        }

        for (const badKind of ['urgent', '', 'HELP', 'info']) {
          const rejected = await client.callTool({
            name: 'xenon_attention',
            arguments: { ...scope, ownershipGeneration: 2, action: 'request', kind: badKind },
          });
          assert.equal(rejected.isError, true, `xenon_attention must reject kind=${JSON.stringify(badKind)}`);
        }
      });

      await t.test('xenon_upload three mutually exclusive modes (direct, fulfill chooser, cancel chooser)', async () => {
        for (const args of [
          { ...mutation, observationId: 'obs-1', elementRef: 'ref-1', fileId: 'file-1' },
          { ...mutation, chooserId: 'chooser-1', fileId: 'file-1' },
          { ...mutation, chooserId: 'chooser-1', cancel: true },
        ]) {
          const reply = await client.callTool({ name: 'xenon_upload', arguments: args });
          assert.equal(reply.isError, undefined, `xenon_upload should accept ${JSON.stringify(args)}`);
          assert.deepEqual(reply.structuredContent, { method: 'files.upload', params: args });
        }

        for (const bad of [
          { ...mutation, chooserId: 'chooser-1' },
          { ...mutation, chooserId: 'chooser-1', fileId: 'file-1', cancel: true },
          { ...mutation, chooserId: 'chooser-1', cancel: false },
          { ...mutation, chooserId: 'chooser-1', fileId: 'file-1', observationId: 'obs-1' },
          { ...mutation, chooserId: 'chooser-1', fileId: 'file-1', elementRef: 'ref-1' },
          { ...mutation, chooserId: 'chooser-1', cancel: true, observationId: 'obs-1' },
          { ...mutation, observationId: 'obs-1', elementRef: 'ref-1', fileId: 'file-1', cancel: true },
          { ...mutation, observationId: 'obs-1', fileId: 'file-1' },
          { ...mutation, elementRef: 'ref-1', fileId: 'file-1' },
          { ...mutation, observationId: 'obs-1', elementRef: 'ref-1' },
          { ...mutation, cancel: true },
          { ...mutation, chooserId: 'bad chooser', fileId: 'file-1' },
        ]) {
          const rejected = await client.callTool({ name: 'xenon_upload', arguments: bad });
          assert.equal(rejected.isError, true, `xenon_upload must reject ${JSON.stringify(bad)}`);
        }
      });
    } finally {
      await client.close();
    }
  });
}

test('createServer over InMemoryTransport blocks schema-invalid tool calls before broker dispatch and pushes activity updates', async () => {
  const listeners = new Set<(event: BrokerEvent) => void>();
  const dispatched: Array<{ method: string; params: Record<string, unknown> }> = [];
  let activityCalls = 0;

  const transport: BrokerTransport = {
    async call(method, params = {}): Promise<NativeReply> {
      if (method === 'control.activity') {
        activityCalls++;
        return { ok: true, result: { tabs: [] } };
      }
      dispatched.push({ method, params });
      return { ok: true, result: { method, params } };
    },
    onEvent(listener) {
      listeners.add(listener);
      return () => { listeners.delete(listener); };
    },
    close() {},
  };

  const server = createServer(transport, 'legacy');
  const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
  const client = new Client({ name: 'in-memory-functional-test', version: '1.0.0' }, {
    versionNegotiation: { mode: 'legacy' },
  });
  const notifications: unknown[] = [];
  client.setNotificationHandler('notifications/resources/updated', request => {
    notifications.push(request.params);
  });

  try {
    await Promise.all([server.connect(serverTransport), client.connect(clientTransport)]);

    // Schema-invalid calls across all new/extended tools must fail validation without calling transport.call().
    const invalidInvocations: Array<[string, Record<string, unknown>]> = [
      ['xenon_wait_ready', { ...scope, timeoutMs: 50 }],
      ['xenon_wait_ready', { ...scope, timeoutMs: 40_000 }],
      ['xenon_observe', { ...scope, roles: [] }],
      ['xenon_inspect', { ...scope, sinceObservationId: 'bad obs' }],
      ['xenon_interact', { ...mutation, observationId: 'obs-1', action: 'scroll', direction: 'down', observeAfter: { screenshot: true } }],
      ['xenon_wait', { ...scope }],
      ['xenon_screenshot', { ...scope, clip: { x: 0, y: 0, width: 10, height: 10 }, candidateId: 'cand-1' }],
      ['xenon_batch', { ...mutation, observationId: 'obs-1', steps: [{ action: 'key', elementRef: 'btn', key: 'Enter' }] }],
      ['xenon_batch', { ...mutation, observationId: 'obs-1', steps: [{ action: 'scroll', direction: 'sideways' }] }],
      ['xenon_operation', { operationId: 'op-1', agentSessionId: 'bad worker' }],
      ['xenon_attention', { ...scope, ownershipGeneration: 1, action: 'request', kind: 'urgent' }],
    ];
    for (const [name, args] of invalidInvocations) {
      const result = await client.callTool({ name, arguments: args });
      assert.equal(result.isError, true, `${name} must reject invalid input`);
    }
    assert.deepEqual(dispatched, [], 'Schema-rejected calls must never reach BrokerTransport.call');

    // Subscribe to activity updates and verify an unsolicited push event triggers immediate MCP notification with zero activity polls.
    await client.subscribeResource({ uri: ACTIVITY_URI });
    const activityCallsBeforePush = activityCalls;
    const emit = (event: BrokerEvent) => { for (const listener of [...listeners]) listener(event); };
    emit({
      event: 'control.activity',
      tabs: [baseActivityTab({ humanHelpHold: false, attentionResolvedEpoch: 0 })],
    });
    emit({
      event: 'control.activity',
      tabs: [{
        ...baseActivityTab({ humanHelpHold: true, attentionRequested: true, attentionKind: 'help', attentionResolvedEpoch: 0 }),
        attentionReason: 'SECRET_NOTE',
      }],
    });
    // Wait a single macrotask tick for in-memory JSON-RPC delivery.
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(activityCalls, activityCallsBeforePush, 'Push event must notify subscriber without polling control.activity');
    assert.deepEqual(notifications, [{ uri: ACTIVITY_URI }]);
  } finally {
    await client.close();
    await server.close();
  }
});
