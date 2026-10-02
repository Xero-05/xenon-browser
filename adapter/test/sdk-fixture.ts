import { serveStdio } from '@modelcontextprotocol/server/stdio';
import { createServer } from '../src/server.js';
import type { BrokerTransport } from '../src/ipc.js';
const transport: BrokerTransport = {
  async call(method, params = {}) { return { ok: true, result: { method, params } }; },
  close() {},
};
serveStdio(({ era }) => createServer(transport, era), { legacy: 'serve' });
