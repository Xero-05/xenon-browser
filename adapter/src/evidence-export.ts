import { randomUUID } from 'node:crypto';
import { lstat, mkdir, open } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { restrictPrivatePath } from './private-config.js';
import { BrokerError } from './ipc.js';

export interface EvidenceExportSaved {
  requestId: string;
  status: 'saved';
  directory: string;
  files: string[];
}

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

  static async createInRoot(rootDirectory: string, maxBytes = 256 * 1024 * 1024): Promise<EvidenceExporter> {
    if (!Number.isSafeInteger(maxBytes) || maxBytes < 1) throw new BrokerError('INVALID_EXPORT_LIMIT', 'Evidence byte limit must be a positive safe integer.');
    const root = resolve(rootDirectory);
    let stats;
    try { stats = await lstat(root); }
    catch (error) {
      const code = (error as NodeJS.ErrnoException).code;
      throw new BrokerError(code === 'ENOENT' ? 'EVIDENCE_PARENT_MISSING' : 'EVIDENCE_DIRECTORY_UNAVAILABLE',
        'Evidence root requires an existing writable directory.');
    }
    if (stats.isSymbolicLink() || !stats.isDirectory()) {
      throw new BrokerError('EVIDENCE_ROOT_INVALID', 'Evidence root must be an existing real directory, not a link or file.');
    }
    const stamp = new Date().toISOString().replace(/[:.]/g, '-');
    return EvidenceExporter.create(join(root, `${stamp}-${randomUUID()}`), maxBytes);
  }

  private async writeExclusive(name: string, data: string | Uint8Array): Promise<void> {
    const file = await open(join(this.directory, name), 'wx', 0o600);
    try { await file.writeFile(data); await file.sync(); } finally { await file.close(); }
  }

  private async save(name: string, data: string | Uint8Array): Promise<void> {
    const size = typeof data === 'string' ? Buffer.byteLength(data) : data.byteLength;
    if (size > 32 * 1024 * 1024 || this.bytes + size > this.maxBytes)
      throw new BrokerError('EVIDENCE_LIMIT', 'Evidence export byte limit reached. No operation was retried.');
    this.bytes += size; // Reserve synchronously, including concurrent completions.
    await this.writeExclusive(name, data);
  }

  private pngEntries(id: string, result: { content: readonly unknown[] }): Array<{ name: string; data: Buffer }> {
    const entries: Array<{ name: string; data: Buffer }> = [];
    for (const [index, item] of result.content.entries()) {
      if (!item || typeof item !== 'object') continue;
      const image = item as { type?: string; data?: string; mimeType?: string };
      if (image.type === 'image' && typeof image.data === 'string' && image.mimeType === 'image/png') {
        entries.push({ name: `${id}.${index}.png`, data: Buffer.from(image.data, 'base64') });
      }
    }
    return entries;
  }

  preview(id: string, result: { content: readonly unknown[] }): EvidenceExportSaved {
    if (!/^[a-f0-9-]{36}$/.test(id)) throw new BrokerError('INVALID_EXPORT_ID', 'Invalid evidence request ID.');
    const files = [`${id}.request.json`, `${id}.result.json`, ...this.pngEntries(id, result).map(entry => entry.name)];
    return { requestId: id, status: 'saved', directory: this.directory, files };
  }

  async begin(name: string, args: Record<string, unknown>): Promise<string> {
    const id = randomUUID();
    await this.save(`${id}.request.json`, JSON.stringify({ version: 1, requestId: id, receivedAt: new Date().toISOString(), name, arguments: args }) + '\n');
    return id;
  }

  async finish(id: string, result: { content: readonly unknown[] }): Promise<EvidenceExportSaved> {
    if (!/^[a-f0-9-]{36}$/.test(id)) throw new BrokerError('INVALID_EXPORT_ID', 'Invalid evidence request ID.');
    const pngs = this.pngEntries(id, result);
    // Store the exact MCP result, including image content and operation IDs.
    const resultJson = JSON.stringify({ version: 1, requestId: id, receivedAt: new Date().toISOString(), result }) + '\n';
    const resultBytes = Buffer.byteLength(resultJson);
    const totalBytes = resultBytes + pngs.reduce((sum, entry) => sum + entry.data.byteLength, 0);
    if (resultBytes > 32 * 1024 * 1024 || pngs.some(entry => entry.data.byteLength > 32 * 1024 * 1024) || this.bytes + totalBytes > this.maxBytes)
      throw new BrokerError('EVIDENCE_LIMIT', 'Evidence export byte limit reached. No operation was retried.');
    this.bytes += totalBytes; // Reserve synchronously across all finish files.
    for (const entry of pngs) await this.writeExclusive(entry.name, entry.data);
    await this.writeExclusive(`${id}.result.json`, resultJson);
    return {
      requestId: id,
      status: 'saved',
      directory: this.directory,
      files: [`${id}.request.json`, `${id}.result.json`, ...pngs.map(entry => entry.name)],
    };
  }
}

// Modern SDK clients can start a separate discovery-only stdio process. Delay
// directory creation until an actual accepted tool call, so a probe consumes
// neither the destination nor the export budget.
export class DeferredEvidenceExporter {
  private ready?: Promise<EvidenceExporter>;
  constructor(private readonly directory: string, private readonly maxBytes = 256 * 1024 * 1024, private readonly mode: 'dir' | 'root' = 'dir') {}
  static fromRoot(rootDirectory: string, maxBytes = 256 * 1024 * 1024): DeferredEvidenceExporter {
    return new DeferredEvidenceExporter(rootDirectory, maxBytes, 'root');
  }
  async begin(name: string, args: Record<string, unknown>): Promise<string> {
    this.ready ??= this.mode === 'root'
      ? EvidenceExporter.createInRoot(this.directory, this.maxBytes)
      : EvidenceExporter.create(this.directory, this.maxBytes);
    return (await this.ready).begin(name, args);
  }
  async preview(id: string, result: { content: readonly unknown[] }): Promise<EvidenceExportSaved> {
    if (!this.ready) throw new BrokerError('EVIDENCE_NOT_STARTED', 'No evidence request was saved.');
    return (await this.ready).preview(id, result);
  }
  async finish(id: string, result: { content: readonly unknown[] }): Promise<EvidenceExportSaved> {
    if (!this.ready) throw new BrokerError('EVIDENCE_NOT_STARTED', 'No evidence request was saved.');
    return (await this.ready).finish(id, result);
  }
}
