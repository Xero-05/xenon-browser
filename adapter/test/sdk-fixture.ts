import { serveStdio } from '@modelcontextprotocol/server/stdio';
import { createServer } from '../src/server.js';
import type { BrokerTransport } from '../src/ipc.js';
import { DeferredEvidenceExporter } from '../src/evidence-export.js';
const transport: BrokerTransport = {
  async call(method, params = {}) { return { ok: true, result: { method, params } }; },
  close() {},
};
const evidence = process.argv[2] ? new DeferredEvidenceExporter(process.argv[2], Number(process.argv[3] ?? 268435456)) : undefined;
serveStdio(({ era }) => createServer(transport, era, evidence), { legacy: 'serve' });
