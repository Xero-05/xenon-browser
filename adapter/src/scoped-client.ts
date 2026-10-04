import { randomUUID } from 'node:crypto';

export interface ToolReply {
  isError?: boolean;
  structuredContent?: Record<string, unknown>;
  content: unknown[];
}
// Structural interface accepts the official MCP Client; no private pipe access.
export interface ToolClient {
  callTool(request: { name: string; arguments: Record<string, unknown> }): Promise<{ isError?: boolean; structuredContent?: unknown; content: unknown[] }>;
}
export interface TabScope { agentSessionId: string; workspaceId: string; tabId: string }
export interface ActionEvidence { ownershipGeneration: number; observationId: string }
export type BatchStep =
  | { action: 'click'; elementRef: string }
  | { action: 'fill'; elementRef: string; text: string }
  | { action: 'select'; elementRef: string; values: string[] }
  | { action: 'check'; elementRef: string; checked: boolean };

export class XenonToolError extends Error {
  constructor(readonly reply: ToolReply, readonly operationId?: string) {
    super('Xenon rejected the call; inspect reply.structuredContent.error and any operationId before continuing.');
  }
}
export class XenonConnectionError extends Error {
  constructor(readonly operationId: string | undefined, options: ErrorOptions) {
    super('No usable tool reply received. Inspect the retained operationId and current page before deciding on another action.', options);
  }
}
function handle(value: unknown): string {
  if (typeof value !== 'string' || !value || Buffer.byteLength(value) > 256 || /[\x00-\x20\x7f]/.test(value))
    throw new Error('Use the exact opaque handle returned by Xenon.');
  return value;
}
async function invoke(client: ToolClient, name: string, args: Record<string, unknown>): Promise<ToolReply> {
  const operationId = typeof args.operationId === 'string' ? args.operationId : undefined;
  let received: Awaited<ReturnType<ToolClient['callTool']>>;
  try { received = await client.callTool({ name, arguments: structuredClone(args) }); }
  catch (error) { throw new XenonConnectionError(operationId, { cause: error }); }
  if (received.structuredContent !== undefined && (received.structuredContent === null || typeof received.structuredContent !== 'object' || Array.isArray(received.structuredContent)))
    throw new XenonConnectionError(operationId, { cause: new Error('Expected a structured Xenon result object.') });
  const reply: ToolReply = { ...received, structuredContent: received.structuredContent as Record<string, unknown> | undefined };
  if (reply.isError) throw new XenonToolError(reply, operationId);
  return reply;
}
function noScopeOverrides(args: Record<string, unknown>): void {
  for (const key of ['agentSessionId', 'workspaceId', 'tabId'])
    if (Object.hasOwn(args, key)) throw new Error(`Bound scope cannot be overridden: ${key}`);
}
const workspaceTools = new Set(['xenon_tabs', 'xenon_folders', 'xenon_files', 'xenon_downloads']);
const tabTools = new Set(['xenon_observe', 'xenon_inspect', 'xenon_screenshot', 'xenon_control_status', 'xenon_control', 'xenon_accounts',
  'xenon_interact', 'xenon_batch', 'xenon_wait', 'xenon_upload', 'xenon_login', 'xenon_navigate', 'xenon_dialog', 'xenon_tab_close']);

export class ScopedXenonTab {
  readonly scope: Readonly<TabScope>;
  constructor(private readonly client: ToolClient, scope: TabScope) {
    this.scope = Object.freeze({ agentSessionId: handle(scope.agentSessionId), workspaceId: handle(scope.workspaceId), tabId: handle(scope.tabId) });
  }
  call(name: string, args: Record<string, unknown> = {}): Promise<ToolReply> {
    noScopeOverrides(args);
    if (!workspaceTools.has(name) && !tabTools.has(name)) throw new Error('Tool is outside this bound tab scope.');
    const scope = workspaceTools.has(name) ? { agentSessionId: this.scope.agentSessionId, workspaceId: this.scope.workspaceId } : this.scope;
    return invoke(this.client, name, { ...args, ...scope });
  }
  inspect(args: { maxNodes?: number; query?: string } = {}) { return this.call('xenon_inspect', args); }
  observe(args: { maxNodes?: number; query?: string } = {}) { return this.call('xenon_observe', args); }
  status() { return this.call('xenon_control_status'); }
  files(args: { folderId?: string; cursor?: string; query?: string; limit?: number } = {}) { return this.call('xenon_files', args); }
  operation(operationId: string) { return invoke(this.client, 'xenon_operation', { operationId: handle(operationId) }); }

  async batch(steps: BatchStep[], evidence: ActionEvidence, options: { operationId?: string; inspectAfter?: boolean } = {}) {
    // Explicit evidence is supplied by the caller, never silently refreshed or
    // reused across workers. Follow-up inspection is a separate authorized read.
    noScopeOverrides(evidence as unknown as Record<string, unknown>);
    const batch = await this.call('xenon_batch', { steps, observationId: handle(evidence.observationId),
      ownershipGeneration: evidence.ownershipGeneration, operationId: options.operationId ?? randomUUID() });
    if (!options.inspectAfter) return { batch };
    try { return { batch, inspection: await this.inspect() }; }
    catch (error) {
      // The batch outcome remains available even if fresh evidence is denied.
      if (error instanceof XenonToolError) return { batch, inspection: error.reply };
      return { batch, inspectionError: 'Fresh inspection was unavailable. Inspect the retained operation and page before continuing.' };
    }
  }
}

export class ScopedXenonWorker {
  readonly scope: Readonly<{ agentSessionId: string; workspaceId: string }>;
  constructor(private readonly client: ToolClient, scope: { agentSessionId: string; workspaceId: string }) {
    this.scope = Object.freeze({ agentSessionId: handle(scope.agentSessionId), workspaceId: handle(scope.workspaceId) });
  }
  tab(tabId: string) { return new ScopedXenonTab(this.client, { ...this.scope, tabId }); }
  async createTab(url?: string) {
    const reply = await invoke(this.client, 'xenon_tab_create', { ...this.scope, ...(url === undefined ? {} : { url }) });
    if (reply.structuredContent?.workspaceId !== this.scope.workspaceId) throw new Error('Created tab returned a mismatched workspace. Inspect current tabs; do not retry creation.');
    return { tab: this.tab(handle(reply.structuredContent.tabId)), reply };
  }
  retire() { return invoke(this.client, 'xenon_worker_retire', { agentSessionId: this.scope.agentSessionId }); }
}

export async function createScopedWorker(client: ToolClient, name: string, workspaceId?: string) {
  const reply = await invoke(client, 'xenon_worker_create', { name, ...(workspaceId === undefined ? {} : { workspaceId: handle(workspaceId) }) });
  return new ScopedXenonWorker(client, { agentSessionId: handle(reply.structuredContent?.agentSessionId), workspaceId: handle(reply.structuredContent?.workspaceId) });
}
