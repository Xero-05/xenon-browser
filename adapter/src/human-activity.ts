import { BrokerError, type BrokerTransport } from './ipc.js';

export const ACTIVITY_URI = 'xenon://control/activity';
export const ACTIVITY_MESSAGE = 'Human page activity keeps the existing owner and temporarily pauses agent input. Wait until humanPaused is false, then obtain a fresh observation before deciding the next action. Never automatically replay an interrupted or cancelled action. Activity notifications depend on the MCP client and do not force a model interruption.';
const POLL_MS = 1000;
const MAX_TABS = 256;

export interface HumanActivityTab {
  tabId: string;
  workspaceId: string;
  ownerSessionId: string;
  ownershipGeneration: number;
  humanActivityEpoch: number;
  humanPaused: boolean;
  humanPauseUntil: number | null;
  requiresFreshObservation: boolean;
}

type Change = 'pause_started' | 'pause_ended' | 'activity_changed' | 'owner_changed';
const handle = (value: unknown): value is string => typeof value === 'string' && /^[A-Za-z0-9_.:-]{1,256}$/.test(value);
const integer = (value: unknown): value is number => typeof value === 'number' && Number.isSafeInteger(value) && value >= 0;

// Never forward extra native properties: this channel cannot carry page text,
// typed values, titles, URLs, account metadata, or native error messages.
function parseTabs(value: unknown): HumanActivityTab[] | undefined {
  if (!Array.isArray(value) || value.length > MAX_TABS) return;
  const seen = new Set<string>();
  const tabs: HumanActivityTab[] = [];
  for (const row of value) {
    if (!row || typeof row !== 'object' || !handle(row.tabId) || !handle(row.workspaceId) || !handle(row.ownerSessionId) ||
      !integer(row.ownershipGeneration) || !integer(row.humanActivityEpoch) || typeof row.humanPaused !== 'boolean' ||
      (row.humanPauseUntil !== null && !integer(row.humanPauseUntil)) || typeof row.requiresFreshObservation !== 'boolean' || seen.has(row.tabId)) return;
    seen.add(row.tabId);
    tabs.push({ tabId: row.tabId, workspaceId: row.workspaceId, ownerSessionId: row.ownerSessionId,
      ownershipGeneration: row.ownershipGeneration, humanActivityEpoch: row.humanActivityEpoch,
      humanPaused: row.humanPaused, humanPauseUntil: row.humanPauseUntil, requiresFreshObservation: row.requiresFreshObservation });
  }
  return tabs.sort((a, b) => a.tabId.localeCompare(b.tabId));
}

/** One bounded read poll per adapter, with no mutation retries or native events. */
export class HumanActivityMonitor {
  private timer?: ReturnType<typeof setTimeout>;
  private flight?: Promise<void>;
  private stopped = false;
  private started = false;
  private lastAttempt = -Infinity;
  private sampledAtUnixMs: number | null = null;
  private available = false;
  private tabs = new Map<string, HumanActivityTab>();
  private pending = new Map<string, Set<Change>>();
  private noticePending = false;

  constructor(private readonly transport: BrokerTransport, private readonly changed: () => Promise<void>,
    private readonly monotonicNow: () => number = () => performance.now(), private readonly unixNow: () => number = Date.now) {}

  start(): void {
    if (this.started || this.stopped) return;
    this.started = true;
    this.schedule();
  }

  stop(): void {
    this.stopped = true;
    if (this.timer) clearTimeout(this.timer);
    this.timer = undefined;
    this.tabs.clear(); this.pending.clear(); this.noticePending = false;
  }

  private schedule(): void {
    if (this.stopped) return;
    this.timer = setTimeout(() => {
      this.timer = undefined;
      void this.refresh().finally(() => this.schedule());
    }, POLL_MS);
    this.timer.unref();
  }

  async refresh(): Promise<void> {
    if (this.stopped) return;
    if (this.flight) return this.flight;
    if (this.monotonicNow() - this.lastAttempt < POLL_MS) return;
    this.lastAttempt = this.monotonicNow();
    this.flight = this.poll().finally(() => { this.flight = undefined; });
    return this.flight;
  }

  private async poll(): Promise<void> {
    let fresh: HumanActivityTab[] | undefined;
    let disconnected = false;
    try {
      const reply = await this.transport.call('control.activity', {}, 2000);
      if (reply.ok) fresh = parseTabs(reply.result.tabs);
      else disconnected = ['UNAUTHORIZED', 'CLIENT_REVOKED'].includes(reply.error.code);
    } catch (error) {
      disconnected = error instanceof BrokerError && ['DISCONNECTED', 'CONNECTION_ERROR'].includes(error.code);
      // Unavailable status is fixed adapter text; no native error leaks.
    }
    if (this.stopped) return;
    const wasAvailable = this.available;
    this.available = fresh !== undefined;
    this.sampledAtUnixMs = this.unixNow();
    if (!fresh) {
      // Access may have been revoked. Never serve cached tab metadata on failure.
      this.tabs.clear(); this.pending.clear();
      if (wasAvailable) this.noticePending = true;
      if (wasAvailable) await this.notify();
      if (disconnected) {
        this.stopped = true;
        if (this.timer) clearTimeout(this.timer);
        this.timer = undefined;
      }
      return;
    }
    let changed = false;
    const next = new Map(fresh.map(tab => [tab.tabId, tab]));
    for (const tab of fresh) {
      const before = this.tabs.get(tab.tabId);
      const changes: Change[] = [];
      if (tab.humanPaused && !before?.humanPaused) changes.push('pause_started');
      if (before?.humanPaused && !tab.humanPaused) changes.push('pause_ended');
      if (tab.humanActivityEpoch !== (before?.humanActivityEpoch ?? 0)) changes.push('activity_changed');
      if (before && (before.ownerSessionId !== tab.ownerSessionId || before.ownershipGeneration !== tab.ownershipGeneration)) changes.push('owner_changed');
      if (changes.length) {
        const pending = this.pending.get(tab.tabId) ?? new Set<Change>();
        for (const change of changes) pending.add(change);
        this.pending.set(tab.tabId, pending); changed = true;
      }
    }
    for (const tabId of this.tabs.keys()) if (!next.has(tabId)) { this.pending.delete(tabId); changed = true; }
    this.tabs = next;
    if (changed) { this.noticePending = true; await this.notify(); }
  }

  private async notify(): Promise<void> {
    if (this.stopped) return;
    try { await this.changed(); } catch { /* The next tool reply remains the fallback. */ }
  }

  snapshot() {
    return { source: 'xenon_browser_control' as const, available: this.available, sampledAtUnixMs: this.sampledAtUnixMs,
      message: this.available ? ACTIVITY_MESSAGE : 'Human activity status is unavailable. Obtain current control status and fresh evidence before continuing; no action was automatically retried.',
      tabs: [...this.tabs.values()].map(tab => ({ ...tab })),
      changes: [...this.pending].map(([tabId, changes]) => ({ tabId, changes: [...changes] })),
    };
  }

  takeNotice() {
    if (!this.noticePending && ![...this.tabs.values()].some(tab => tab.humanPaused || tab.requiresFreshObservation)) return;
    const snapshot = this.snapshot();
    this.noticePending = false; this.pending.clear();
    return snapshot;
  }
}
