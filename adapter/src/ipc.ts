import net from 'node:net';
import { randomUUID } from 'node:crypto';
import { readFile } from 'node:fs/promises';

export interface NativeError { code: string; message: string }
export type NativeReply = ({ ok: true; result: Record<string, unknown> } | { ok: false; error: NativeError }) & { operationId?: string; dispatchStatus?: string };
export interface ClientConfig { clientId: string; token: string; pipe?: string }
export interface BrokerTransport {
  call(method: string, params?: Record<string, unknown>, timeoutMs?: number): Promise<NativeReply>;
  close(): void;
}
export const DEFAULT_PIPE = '\\\\.\\pipe\\xenon-browser';
const MAX_MESSAGE_BYTES = 16 * 1024 * 1024;

export class BrokerError extends Error {
  constructor(public readonly code: string, message: string) { super(message); }
}

export async function readClientConfig(path: string): Promise<ClientConfig> {
  const parsed: unknown = JSON.parse(await readFile(path, 'utf8'));
  if (!parsed || typeof parsed !== 'object') throw new BrokerError('INVALID_CONFIG', 'Invalid client configuration.');
  const c = parsed as Partial<ClientConfig>;
  if (typeof c.clientId !== 'string' || typeof c.token !== 'string' || !c.clientId || !c.token)
    throw new BrokerError('INVALID_CONFIG', 'Client configuration must contain clientId and token.');
  if (c.pipe !== undefined && (typeof c.pipe !== 'string' || !c.pipe.startsWith('\\\\.\\pipe\\')))
    throw new BrokerError('INVALID_CONFIG', 'Only local Windows named pipes are supported.');
  return c as ClientConfig;
}

export class PipeTransport implements BrokerTransport {
  private socket?: net.Socket;
  private buffer = Buffer.alloc(0);
  private pending = new Map<string, { resolve: (value: NativeReply) => void; reject: (error: Error) => void; timer: NodeJS.Timeout }>();
  private closed = false;

  constructor(private readonly pipe = DEFAULT_PIPE) {}

  async connect(config?: ClientConfig): Promise<void> {
    if (this.socket) throw new BrokerError('ALREADY_CONNECTED', 'Transport is already connected.');
    this.closed = false;
    const socket = net.createConnection(this.pipe);
    this.socket = socket;
    socket.on('data', data => this.receive(data));
    socket.on('close', () => this.failPending(new BrokerError('DISCONNECTED', 'Browser connection closed; inspect operation status before retrying an action.')));
    socket.on('error', () => this.failPending(new BrokerError('CONNECTION_ERROR', 'Cannot connect to Xenon. Start the browser and check the pipe configuration.')));
    await new Promise<void>((resolve, reject) => {
      const timer = setTimeout(() => { socket.destroy(); reject(new BrokerError('CONNECT_TIMEOUT', 'Browser connection timed out.')); }, 10_000);
      socket.once('connect', () => { clearTimeout(timer); resolve(); });
      socket.once('error', () => { clearTimeout(timer); reject(new BrokerError('CONNECTION_ERROR', 'Cannot connect to Xenon. Start the browser first.')); });
    });
    if (config) {
      const reply = await this.call('hello', { clientId: config.clientId, token: config.token });
      if (!reply.ok) { this.close(); throw new BrokerError(reply.error.code, reply.error.message); }
    }
  }

  call(method: string, params: Record<string, unknown> = {}, timeoutMs = 65_000): Promise<NativeReply> {
    if (!this.socket || this.socket.destroyed || this.closed)
      return Promise.reject(new BrokerError('DISCONNECTED', 'Browser connection is not open.'));
    if (this.pending.size >= 128) return Promise.reject(new BrokerError('CAPACITY_EXCEEDED', 'Too many outstanding browser operations.'));
    const id = randomUUID();
    const message = JSON.stringify({ id, method, params }) + '\n';
    if (Buffer.byteLength(message) > 1024 * 1024)
      return Promise.reject(new BrokerError('MESSAGE_TOO_LARGE', 'Operation exceeds the transport limit.'));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new BrokerError('OUTCOME_UNKNOWN', 'No response received. The action may have been dispatched; query its operationId before retrying.'));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.socket!.write(message, error => {
        if (!error) return;
        const item = this.pending.get(id);
        if (item) { clearTimeout(item.timer); this.pending.delete(id); item.reject(new BrokerError('OUTCOME_UNKNOWN', 'Connection failed while sending an operation.')); }
      });
    });
  }

  private receive(chunk: Buffer): void {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const end = this.buffer.indexOf(10);
      if (end < 0) break;
      if (end > MAX_MESSAGE_BYTES) { this.protocolFailure(); return; }
      const line = this.buffer.subarray(0, end).toString('utf8');
      this.buffer = this.buffer.subarray(end + 1);
      let result: unknown;
      try { result = JSON.parse(line); } catch { this.protocolFailure(); return; }
      if (!result || typeof result !== 'object') { this.protocolFailure(); return; }
      const response = result as Record<string, unknown>;
      if (typeof response.id !== 'string' || typeof response.ok !== 'boolean') { this.protocolFailure(); return; }
      const item = this.pending.get(response.id);
      if (!item) continue; // Late response to an expired call; never resubmit it.
      if (!response.ok && (!response.error || typeof response.error !== 'object')) { this.protocolFailure(); return; }
      this.pending.delete(response.id);
      clearTimeout(item.timer);
      item.resolve(response as unknown as NativeReply);
    }
    if (this.buffer.length > MAX_MESSAGE_BYTES) this.protocolFailure();
  }

  private protocolFailure(): void {
    this.failPending(new BrokerError('INVALID_RESPONSE', 'Browser returned an invalid transport message.'));
    this.close();
  }
  private failPending(error: Error): void {
    this.closed = true;
    for (const item of this.pending.values()) { clearTimeout(item.timer); item.reject(error); }
    this.pending.clear();
  }
  close(): void {
    this.failPending(new BrokerError('DISCONNECTED', 'Browser transport closed.'));
    this.socket?.destroy();
    this.socket = undefined;
    this.buffer = Buffer.alloc(0);
  }
}
