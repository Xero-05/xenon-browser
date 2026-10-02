#!/usr/bin/env node
import { parseArgs } from 'node:util';
import { mkdir } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { serveStdio } from '@modelcontextprotocol/server/stdio';
import { BrokerError, DEFAULT_PIPE, PipeTransport, readClientConfig } from './ipc.js';
import { createServer } from './server.js';
import { writePrivateConfig } from './private-config.js';

const { values, positionals } = parseArgs({ allowPositionals: true, options: {
  config: { type: 'string' }, pipe: { type: 'string' }, name: { type: 'string' }, output: { type: 'string' }, help: { type: 'boolean' },
}});
async function main() {
  if (values.help) {
    process.stderr.write('Xenon MCP\n  pair --name "Agent app" --output <private-config.json> [--pipe <local-pipe>]\n  serve --config <private-config.json>\nPairing requires approval in the native Xenon control window. Keep the configuration private.\n');
    return;
  }
  if (positionals[0] === 'pair') {
    if (!values.output || !values.name) throw new BrokerError('ARGUMENT_REQUIRED', 'pair requires --name and --output.');
    const pipe = values.pipe ?? DEFAULT_PIPE;
    if (!pipe.startsWith('\\\\.\\pipe\\')) throw new BrokerError('INVALID_PIPE', 'Only local named pipes are supported.');
    const transport = new PipeTransport(pipe);
    await transport.connect();
    process.stderr.write('Approve this connection in Xenon Controls.\n');
    try {
      const result = await transport.call('pair.request', { name: values.name }, 300_000);
      if (!result.ok) throw new BrokerError(result.error.code, result.error.message);
      const config = { clientId: result.result.clientId, token: result.result.token, pipe };
      if (typeof config.clientId !== 'string' || typeof config.token !== 'string') throw new BrokerError('INVALID_RESPONSE', 'Pairing response is invalid.');
      const path = resolve(values.output);
      await mkdir(dirname(path), { recursive: true });
      await writePrivateConfig(path, config as { clientId: string; token: string; pipe: string });
      process.stderr.write('Pairing saved. Configure your MCP client to run this adapter with serve --config and the private configuration path.\n');
    } finally { transport.close(); }
    return;
  }
  if (!values.config) throw new BrokerError('ARGUMENT_REQUIRED', 'serve requires --config. Run pair first.');
  const config = await readClientConfig(values.config);
  const transport = new PipeTransport(values.pipe ?? config.pipe ?? DEFAULT_PIPE);
  await transport.connect(config);
  process.on('SIGINT', () => { transport.close(); process.exit(0); });
  process.on('SIGTERM', () => { transport.close(); process.exit(0); });
  process.stdin.on('end', () => transport.close());
  await serveStdio(({ era }) => createServer(transport, era), { legacy: 'serve' });
}
main().catch(error => {
  const safe = error instanceof BrokerError ? `${error.code}: ${error.message}` : 'STARTUP_ERROR: Xenon MCP could not start. Check configuration and browser availability.';
  process.stderr.write(safe + '\n');
  process.exitCode = 1;
});
