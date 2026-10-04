import { randomUUID } from 'node:crypto';
import { mkdir, open } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { restrictPrivatePath } from './private-config.js';
import { BrokerError } from './ipc.js';

// Explicit host opt-in only. This records public MCP calls, never pairing or
// internal native credential traffic. Each run uses a new private directory.
export class EvidenceExporter {
  private bytes = 0;
  private constructor(readonly directory: string, private readonly maxBytes: number) {}

  static async create(directory: string, maxBytes = 256 * 1024 * 1024): Promise<EvidenceExporter> {
    if (!Number.isSafeInteger(maxBytes) || maxBytes < 1) throw new BrokerError('INVALID_EXPORT_LIMIT', 'Evidence byte limit must be a positive safe integer.');
    const path = resolve(directory);
    try { await mkdir(path, { mode: 0o700 }); } // Refuse existing directories, including links.
    catch (error) {
      const code = (error as NodeJS.ErrnoException).code;
      throw new BrokerError(code === 'EEXIST' ? 'EVIDENCE_DIRECTORY_EXISTS' : code === 'ENOENT' ? 'EVIDENCE_PARENT_MISSING' : 'EVIDENCE_DIRECTORY_UNAVAILABLE',
        'Evidence export requires a new writable directory with an existing parent.');
    }
    try { await restrictPrivatePath(path, true); }
    catch { throw new BrokerError('EVIDENCE_ACL_FAILED', 'Could not protect the evidence directory for the current Windows user.'); }
    return new EvidenceExporter(path, maxBytes);
  }

  private async save(name: string, data: string | Uint8Array): Promise<void> {
    const size = typeof data === 'string' ? Buffer.byteLength(data) : data.byteLength;
    if (size > 32 * 1024 * 1024 || this.bytes + size > this.maxBytes)
      throw new BrokerError('EVIDENCE_LIMIT', 'Evidence export byte limit reached. No operation was retried.');
    this.bytes += size; // Reserve synchronously, including concurrent completions.
    const file = await open(join(this.directory, name), 'wx', 0o600);
    try { await file.writeFile(data); await file.sync(); } finally { await file.close(); }
  }

  async begin(name: string, args: Record<string, unknown>): Promise<string> {
    const id = randomUUID();
    await this.save(`${id}.request.json`, JSON.stringify({ version: 1, requestId: id, receivedAt: new Date().toISOString(), name, arguments: args }) + '\n');
    return id;
  }

  async finish(id: string, result: { content: readonly unknown[] }): Promise<void> {
    if (!/^[a-f0-9-]{36}$/.test(id)) throw new BrokerError('INVALID_EXPORT_ID', 'Invalid evidence request ID.');
    // Store the exact MCP result, including image content and operation IDs.
    await this.save(`${id}.result.json`, JSON.stringify({ version: 1, requestId: id, receivedAt: new Date().toISOString(), result }) + '\n');
    for (const [index, item] of result.content.entries()) {
      if (!item || typeof item !== 'object') continue;
      const image = item as { type?: string; data?: string; mimeType?: string };
      if (image.type === 'image' && typeof image.data === 'string' && image.mimeType === 'image/png')
        await this.save(`${id}.${index}.png`, Buffer.from(image.data, 'base64'));
    }
  }
}

// Modern SDK clients can start a separate discovery-only stdio process. Delay
// directory creation until an actual accepted tool call, so a probe consumes
// neither the destination nor the export budget.
export class DeferredEvidenceExporter {
  private ready?: Promise<EvidenceExporter>;
  constructor(private readonly directory: string, private readonly maxBytes = 256 * 1024 * 1024) {}
  async begin(name: string, args: Record<string, unknown>): Promise<string> {
    this.ready ??= EvidenceExporter.create(this.directory, this.maxBytes);
    return (await this.ready).begin(name, args);
  }
  async finish(id: string, result: { content: readonly unknown[] }): Promise<void> {
    if (!this.ready) throw new BrokerError('EVIDENCE_NOT_STARTED', 'No evidence request was saved.');
    await (await this.ready).finish(id, result);
  }
}
