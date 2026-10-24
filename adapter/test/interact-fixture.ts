import { serveStdio } from '@modelcontextprotocol/server/stdio';
import { createServer } from '../src/server.js';
import type { BrokerTransport } from '../src/ipc.js';

let pageCalls = 0;
const transport: BrokerTransport = {
  async call(method, params = {}) {
    if (method.startsWith('page.')) pageCalls++;
    if (params.elementRef === 'native-stale') return {
      ok: false, error: { code: 'stale_element', message: 'Observe the page again before using this element.' },
      operationId: String(params.operationId), dispatchStatus: 'dispatched',
    };
    return { ok: true, result: { method, params, pageCalls } };
  },
  close() {},
};
serveStdio(({ era }) => createServer(transport, era), { legacy: 'serve' });
