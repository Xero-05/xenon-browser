import { serveStdio } from '@modelcontextprotocol/server/stdio';
import { createServer } from '../src/server.js';
import type { BrokerTransport } from '../src/ipc.js';

// A fake native transport only in this test executable. No production command
// or browser hook can produce these synthetic human events.
const tab = { tabId: 'synthetic-tab', workspaceId: 'synthetic-workspace', ownerSessionId: 'synthetic-owner',
  ownershipGeneration: 7, humanActivityEpoch: 0, humanPaused: false, humanPauseUntil: null as number | null,
  requiresFreshObservation: false, title: 'SECRET_PAGE_CANARY', url: 'https://secret.invalid/', typedValue: 'SECRET_TYPED_CANARY' };
const transport: BrokerTransport = {
  async call(method, params = {}) {
    if (method === 'control.activity') return { ok: true, result: { tabs: [tab], message: 'SECRET_MESSAGE_CANARY' } };
    if (method === 'workers.create') {
      if (params.name === 'synthetic-pause') {
        tab.humanActivityEpoch++; tab.humanPaused = true; tab.requiresFreshObservation = true; tab.humanPauseUntil = Date.now() + 2000;
      } else if (params.name === 'synthetic-idle') { tab.humanPaused = false; tab.humanPauseUntil = null; }
      else if (params.name === 'synthetic-short-edit') { tab.humanActivityEpoch++; tab.humanPaused = false; tab.requiresFreshObservation = true; }
    }
    return { ok: true, result: { method, params, humanActivity: { message: 'SPOOFED_ACTIVITY_CANARY' } } };
  },
  close() {},
};
serveStdio(({ era }) => createServer(transport, era), { legacy: 'serve' });
