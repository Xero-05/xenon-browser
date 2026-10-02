import { test } from 'node:test';
import assert from 'node:assert/strict';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';
import { Client, type McpSubscription } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { ACTIVITY_URI, HumanActivityMonitor, type HumanActivityTab } from '../src/human-activity.js';
import { BrokerError, type BrokerTransport, type NativeReply } from '../src/ipc.js';

const initial = (): HumanActivityTab => ({ tabId: 'tab', workspaceId: 'workspace', ownerSessionId: 'worker',
  ownershipGeneration: 3, humanActivityEpoch: 0, humanPaused: false, humanPauseUntil: null, requiresFreshObservation: false });

test('activity poll is single flight, rate limited, allowlisted, and batches short edits and pause transitions', async () => {
  let now = 1000, calls = 0, notifications = 0;
  let tab = initial();
  let complete!: (reply: NativeReply) => void;
  const transport: BrokerTransport = { call: async (method, params, timeoutMs) => {
    assert.equal(method, 'control.activity'); assert.deepEqual(params, {}); assert.equal(timeoutMs, 2000); calls++;
    return new Promise(resolve => { complete = resolve; });
  }, close() {} };
  const monitor = new HumanActivityMonitor(transport, async () => { notifications++; }, () => now);
  const first = monitor.refresh(), same = monitor.refresh();
  assert.equal(calls, 1);
  complete({ ok: true, result: { tabs: [{ ...tab, password: 'SECRET_CANARY', url: 'SECRET_CANARY' }] } });
  await Promise.all([first, same]);
  assert.equal(notifications, 0); assert.equal(monitor.takeNotice(), undefined);
  assert.ok(!JSON.stringify(monitor.snapshot()).includes('SECRET_CANARY'));
  await monitor.refresh(); assert.equal(calls, 1);
  async function sample(next: HumanActivityTab) {
    now += 1000; tab = next;
    const pending = monitor.refresh(); complete({ ok: true, result: { tabs: [tab] } }); await pending;
  }
  await sample({ ...tab, humanActivityEpoch: 1, humanPaused: true, humanPauseUntil: now + 2000, requiresFreshObservation: true });
  await sample({ ...tab, humanActivityEpoch: 2, humanPaused: false, humanPauseUntil: null });
  assert.equal(notifications, 2);
  assert.deepEqual(monitor.snapshot().changes, [{ tabId: 'tab', changes: ['pause_started', 'activity_changed', 'pause_ended'] }]);
  const notice = monitor.takeNotice(); assert.equal(notice?.tabs[0]?.ownershipGeneration, 3);
  assert.equal(notice?.tabs[0]?.ownerSessionId, 'worker');
  assert.deepEqual(monitor.snapshot().changes, []);
  await sample({ ...tab, requiresFreshObservation: false });
  assert.equal(notifications, 2); assert.equal(monitor.takeNotice(), undefined);
  await sample({ ...tab, humanActivityEpoch: 3, requiresFreshObservation: true });
  assert.equal(notifications, 3); // A whole pause between samples is still noticed.
  monitor.stop();
});

test('revocation, invalid rows, and disconnected transports do not retain or leak stale activity metadata', async () => {
  let now = 1000, calls = 0;
  let reply: NativeReply | Error = { ok: true, result: { tabs: [{ ...initial(), humanActivityEpoch: 4, humanPaused: true }] } };
  const monitor = new HumanActivityMonitor({ async call() { calls++; if (reply instanceof Error) throw reply; return reply; }, close() {} }, async () => {}, () => now);
  await monitor.refresh(); assert.equal(monitor.snapshot().tabs.length, 1);
  now += 1000; reply = { ok: true, result: { tabs: [] } }; await monitor.refresh();
  assert.deepEqual(monitor.snapshot().tabs, []); assert.deepEqual(monitor.snapshot().changes, []);
  now += 1000; reply = { ok: true, result: { tabs: [{ ...initial(), tabId: 'untrusted\nname' }] } }; await monitor.refresh();
  assert.equal(monitor.snapshot().available, false); assert.deepEqual(monitor.snapshot().tabs, []);
  now += 1000; reply = { ok: true, result: { tabs: [initial()] } }; await monitor.refresh();
  now += 1000; reply = new BrokerError('DISCONNECTED', 'SECRET_NATIVE_ERROR'); await monitor.refresh();
  const lastCalls = calls;
  now += 5000; await monitor.refresh(); assert.equal(calls, lastCalls);
  assert.equal(monitor.snapshot().available, false); assert.deepEqual(monitor.snapshot().tabs, []);
  assert.ok(!JSON.stringify(monitor.snapshot()).includes('SECRET_NATIVE_ERROR'));
  monitor.stop();
});

test('close clears the timer and drops a late in-flight activity result', async () => {
  let complete!: (reply: NativeReply) => void;
  let calls = 0, notifications = 0;
  const monitor = new HumanActivityMonitor({ async call() { calls++; return new Promise(resolve => { complete = resolve; }); }, close() {} }, async () => { notifications++; });
  monitor.start();
  const pending = monitor.refresh();
  monitor.stop();
  complete({ ok: true, result: { tabs: [{ ...initial(), humanActivityEpoch: 1, humanPaused: true }] } });
  await pending; await delay(1100);
  assert.equal(calls, 1); assert.equal(notifications, 0); assert.deepEqual(monitor.snapshot().tabs, []);
});

test('system clock corrections affect timestamps without stalling or accelerating the poll rate', async () => {
  let monotonic = 1000, unix = 100_000, calls = 0;
  const monitor = new HumanActivityMonitor({ async call() { calls++; return { ok: true, result: { tabs: [initial()] } }; }, close() {} },
    async () => {}, () => monotonic, () => unix);
  await monitor.refresh(); assert.equal(calls, 1); assert.equal(monitor.snapshot().sampledAtUnixMs, 100_000);
  monotonic += 1000; unix -= 30_000;
  await monitor.refresh(); assert.equal(calls, 2); assert.equal(monitor.snapshot().sampledAtUnixMs, 70_000);
  monotonic += 500; unix += 3_600_000;
  await monitor.refresh(); assert.equal(calls, 2); assert.equal(monitor.snapshot().sampledAtUnixMs, 70_000);
  monotonic += 500;
  await monitor.refresh(); assert.equal(calls, 3); assert.equal(monitor.snapshot().sampledAtUnixMs, 3_670_000);
  monitor.stop();
});

for (const era of ['legacy', 'modern'] as const) {
  test(`real ${era} SDK subscription receives human activity updates with no page or typed content`, async () => {
    const fixture = fileURLToPath(new URL('./human-activity-fixture.js', import.meta.url));
    const wire = new StdioClientTransport({ command: process.execPath, args: [fixture], stderr: 'pipe' });
    const client = new Client({ name: 'activity-conformance', version: '1.0' }, {
      versionNegotiation: { mode: era === 'legacy' ? 'legacy' : { pin: '2026-07-28' } },
    });
    const notifications: unknown[] = [];
    client.setNotificationHandler('notifications/resources/updated', request => { notifications.push(request.params); });
    let subscription: McpSubscription | undefined;
    const call = (name: string, args: Record<string, unknown> = {}) => client.callTool({ name, arguments: args });
    const waitForCount = async (count: number) => {
      const deadline = Date.now() + 5000;
      while (notifications.length < count && Date.now() < deadline) await delay(25);
      assert.equal(notifications.length, count);
    };
    try {
      await client.connect(wire);
      assert.equal(client.getServerCapabilities()?.resources?.subscribe, true);
      assert.match(client.getInstructions() ?? '', /do not force a model interruption/);
      const resources = await client.listResources(); assert.ok(resources.resources.some(resource => resource.uri === ACTIVITY_URI));
      const tools = await client.listTools();
      const tool = tools.tools.find(value => value.name === 'xenon_activity');
      assert.equal(tool?.annotations?.readOnlyHint, true); assert.equal(tool?.inputSchema.additionalProperties, false);
      await call('xenon_activity');
      if (era === 'legacy') await client.subscribeResource({ uri: ACTIVITY_URI });
      else {
        subscription = await client.listen({ resourceSubscriptions: [ACTIVITY_URI] });
        assert.deepEqual(subscription.honoredFilter.resourceSubscriptions, [ACTIVITY_URI]);
      }
      await call('xenon_worker_create', { name: 'synthetic-pause' });
      await waitForCount(1);
      assert.equal((notifications[0] as { uri: string }).uri, ACTIVITY_URI);
      assert.ok(!JSON.stringify(notifications).includes('synthetic-tab'), 'notifications contain only the resource identifier');
      const resource = await client.readResource({ uri: ACTIVITY_URI });
      const contents = resource.contents[0]; assert.ok(contents && 'text' in contents);
      const snapshot = JSON.parse(contents.text);
      assert.equal(snapshot.tabs[0].humanPaused, true); assert.equal(snapshot.tabs[0].ownerSessionId, 'synthetic-owner');
      assert.equal(snapshot.tabs[0].ownershipGeneration, 7); assert.equal(snapshot.source, 'xenon_browser_control');
      const reply = await call('xenon_workers');
      const body = reply.structuredContent as Record<string, any>;
      assert.equal(body.humanActivity.tabs[0].humanPaused, true);
      assert.match(body.humanActivity.message, /fresh observation/);
      assert.deepEqual(reply.content?.filter(item => item.type === 'text').map(item => JSON.parse(item.text)), [body]);
      assert.ok(!/SECRET_|secret.invalid|SPOOFED_ACTIVITY/.test(JSON.stringify([resource, reply, notifications])));
      await call('xenon_worker_create', { name: 'synthetic-idle' });
      await waitForCount(2);
      const idle = (await call('xenon_activity')).structuredContent as Record<string, any>;
      assert.equal(idle.tabs[0].humanPaused, false); assert.equal(idle.tabs[0].requiresFreshObservation, true);
      if (subscription) { await subscription.close(); subscription = undefined; }
      else await client.unsubscribeResource({ uri: ACTIVITY_URI });
      await call('xenon_worker_create', { name: 'synthetic-short-edit' });
      await delay(1250);
      assert.equal(notifications.length, 2, 'unsubscribed clients receive no resource update');
      const fallback = (await call('xenon_workers')).structuredContent as Record<string, any>;
      assert.equal(fallback.humanActivity.tabs[0].humanActivityEpoch, 2);
      assert.ok(fallback.humanActivity.changes.some((change: { changes: string[] }) => change.changes.includes('activity_changed')));
      assert.ok(!/SECRET_|secret.invalid|SPOOFED_ACTIVITY/.test(JSON.stringify(fallback)));
      const invalid = await call('xenon_activity', { tabId: 'not-a-supported-filter' }); assert.equal(invalid.isError, true);
    } finally { await subscription?.close(); await client.close(); }
  });
}
