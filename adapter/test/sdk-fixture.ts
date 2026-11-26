import { serveStdio } from '@modelcontextprotocol/server/stdio';
import { createServer } from '../src/server.js';
import type { BrokerTransport } from '../src/ipc.js';
import { DeferredEvidenceExporter } from '../src/evidence-export.js';
const args = process.argv.slice(2);
const returnImage = args.includes('--image');
const positional = args.filter(a => a !== '--image');
const transport: BrokerTransport = {
  async call(method, params = {}) {
    if (returnImage && (method === 'page.screenshot' || method === 'page.inspect')) {
      return { ok: true, result: { method, params, mimeType: 'image/png', data: 'aGVsbG8=', observationId: 'obs-img' } };
    }
    return { ok: true, result: { method, params } };
  },
  close() {},
};
let evidence: DeferredEvidenceExporter | undefined;
if (positional[0] === '--root' && positional[1]) {
  evidence = DeferredEvidenceExporter.fromRoot(positional[1], Number(positional[2] ?? 268435456));
} else if (positional[0]) {
  evidence = new DeferredEvidenceExporter(positional[0], Number(positional[1] ?? 268435456));
}
serveStdio(({ era }) => createServer(transport, era, evidence), { legacy: 'serve' });
