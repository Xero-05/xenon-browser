// Exercise the real PowerShell helper and curl against disposable loopback HTTP
// fixtures. No browser, npm dependency, internet download or user profile is used.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash, randomUUID } from 'node:crypto';
import { copyFile, mkdir, mkdtemp, readFile, readdir, writeFile } from 'node:fs/promises';
import http from 'node:http';
import { resolve } from 'node:path';
import test from 'node:test';

const root = resolve(import.meta.dirname, '..');
const helper = resolve(root, 'scripts/dependency-download.ps1');
const cache = resolve(root, '.cache');
const payload = Buffer.from('XENON_SYNTHETIC_DEPENDENCY_FIXTURE\n'.repeat(2048));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const expectedHash = sha256(payload);
const marker = 'XENON_DOWNLOAD_TEST_RESULT:';
const options = { skip: process.platform !== 'win32', timeout: 25_000 };
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

const wrapper = String.raw`param([string]$HelperPath, [string]$FixtureConfig)
$ErrorActionPreference = 'Stop'
$taskClock = [Diagnostics.Stopwatch]::StartNew()
try {
  . $HelperPath
  $taskConfig = Get-Content -LiteralPath $FixtureConfig -Raw | ConvertFrom-Json
  $taskCurlMatches = @(Get-Command curl.exe -CommandType Application -ErrorAction Stop)
  $taskArguments = @{
    Url = [string]$taskConfig.url
    Name = [string]$taskConfig.name
    ExpectedHash = [string]$taskConfig.expectedHash
    DownloadDirectory = [string]$taskConfig.downloadDirectory
    ConnectTimeoutSeconds = [int]$taskConfig.connectTimeoutSeconds
    StallTimeoutSeconds = [int]$taskConfig.stallTimeoutSeconds
    AttemptTimeoutSeconds = [int]$taskConfig.attemptTimeoutSeconds
    MaxAttempts = [int]$taskConfig.maxAttempts
    RetryDelaySeconds = [int]$taskConfig.retryDelaySeconds
  }
  if ($taskConfig.allowLoopbackHttp) { $taskArguments.AllowLoopbackHttp = $true }
  $taskClock.Restart()
  $taskValues = @(Get-XenonDependency @taskArguments)
  $taskClock.Stop()
  $taskResult = @{ ok=$true; paths=@($taskValues); elapsedMs=$taskClock.ElapsedMilliseconds; curlApplicationCount=$taskCurlMatches.Count }
  [Console]::Out.WriteLine('XENON_DOWNLOAD_TEST_RESULT:' + ($taskResult | ConvertTo-Json -Compress -Depth 4))
  exit 0
} catch {
  $taskClock.Stop()
  $taskResult = @{ ok=$false; error=$_.Exception.Message; elapsedMs=$taskClock.ElapsedMilliseconds }
  [Console]::Out.WriteLine('XENON_DOWNLOAD_TEST_RESULT:' + ($taskResult | ConvertTo-Json -Compress -Depth 4))
  exit 10
}
`;

async function until(check, timeoutMs = 2000) {
  const deadline = performance.now() + timeoutMs;
  do { if (await check()) return; await sleep(25); } while (performance.now() < deadline);
  throw new Error('Loopback fixture did not reach the expected state within its bound.');
}

async function fixture(t, respond) {
  await mkdir(cache, { recursive: true });
  const directory = await mkdtemp(resolve(cache, 'bootstrap-download-'));
  const downloads = resolve(directory, 'downloads');
  await mkdir(downloads);
  const wrapperPath = resolve(directory, 'invoke.ps1');
  await writeFile(wrapperPath, wrapper, 'utf8');
  let requests = 0;
  const children = new Set();
  const timers = new Set();
  const server = http.createServer((req, res) => {
    requests++;
    res.setHeader('Connection', 'close');
    respond(req, res, requests, timers);
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const url = `http://127.0.0.1:${server.address().port}/artifact`;
  async function closeServer() {
    for (const timer of timers) clearTimeout(timer);
    timers.clear();
    server.closeAllConnections();
    if (server.listening) await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  }
  // Only test-owned process trees can be stopped by the watchdog. No browsing
  // processes are started, enumerated or terminated by this suite.
  const stop = child => new Promise(resolve => {
    if (child.exitCode !== null || child.signalCode !== null || !child.pid) return resolve();
    const killer = spawn('taskkill.exe', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' });
    killer.once('error', resolve); killer.once('close', resolve);
  });
  t.after(async () => {
    await Promise.all([...children].map(stop));
    await closeServer();
  });
  async function download(overrides = {}, environment = {}) {
    const config = {
      url, name: 'artifact.bin', expectedHash, downloadDirectory: downloads,
      connectTimeoutSeconds: 1, stallTimeoutSeconds: 1, attemptTimeoutSeconds: 3,
      maxAttempts: 3, retryDelaySeconds: 0, allowLoopbackHttp: true, ...overrides,
    };
    const configPath = resolve(directory, `${randomUUID()}.json`);
    await writeFile(configPath, JSON.stringify(config), 'utf8');
    const childEnvironment = { ...process.env };
    // Windows environment names are case-insensitive. Avoid leaving both Path
    // and PATH, which Node may resolve in a different order than the override.
    for (const [name, value] of Object.entries(environment)) {
      for (const existing of Object.keys(childEnvironment)) if (existing.toLowerCase() === name.toLowerCase()) delete childEnvironment[existing];
      childEnvironment[name] = value;
    }
    const result = await new Promise((resolve, reject) => {
      const child = spawn(process.env.XENON_TEST_POWERSHELL || 'pwsh.exe', [
        '-NoProfile', '-NonInteractive', '-File', wrapperPath, '-HelperPath', helper, '-FixtureConfig', configPath,
      ], { cwd: root, env: childEnvironment, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
      children.add(child);
      let stdout = '', stderr = '', timedOut = false;
      child.stdout.setEncoding('utf8'); child.stderr.setEncoding('utf8');
      child.stdout.on('data', chunk => { stdout += chunk; });
      child.stderr.on('data', chunk => { stderr += chunk; });
      const watchdog = setTimeout(() => { timedOut = true; void stop(child); }, 10_000);
      child.once('error', error => { clearTimeout(watchdog); children.delete(child); reject(error); });
      child.once('close', code => {
        clearTimeout(watchdog); children.delete(child);
        if (timedOut) return reject(new Error(`Downloader exceeded the 10-second fixture watchdog.\n${stdout}\n${stderr}`));
        const lines = stdout.split(/\r?\n/u).filter(line => line.startsWith(marker));
        if (lines.length !== 1) return reject(new Error(`Expected exactly one wrapper result (exit ${code}).\n${stdout}\n${stderr}`));
        try { resolve({ ...JSON.parse(lines[0].slice(marker.length)), code, stdout, stderr }); }
        catch (error) { reject(error); }
      });
    });
    assert.equal(result.code, result.ok ? 0 : 10, 'Failure must come from the helper, not a crashed test wrapper.');
    return result;
  }
  return { download, directory, downloads, url, target: resolve(downloads, 'artifact.bin'), closeServer, get requests() { return requests; } };
}

function servePayload(_req, res) { res.writeHead(200, { 'Content-Length': payload.length }); res.end(payload); }
async function assertSuccess(f, result) {
  assert.equal(result.ok, true, result.error ?? result.stderr);
  assert.deepEqual(result.paths, [f.target], 'Success stream must contain only the validated final path.');
  assert.deepEqual(await readFile(f.target), payload);
  assert.deepEqual(await readdir(f.downloads), ['artifact.bin'], 'Successful downloads leave no partial files.');
}
async function assertFailure(f, result) {
  assert.equal(result.ok, false, 'The helper must reject this transfer.');
  assert.match(result.error, /\S/u, 'Failure must include a diagnostic.');
  assert.deepEqual(await readdir(f.downloads), [], 'Failed downloads must publish no final file and remove every partial.');
}

test('downloads exact bytes, then validates the cached hash with the server offline', options, async t => {
  const f = await fixture(t, servePayload);
  await assertSuccess(f, await f.download({ expectedHash: expectedHash.toUpperCase() }));
  assert.equal(f.requests, 1);
  await f.closeServer();
  await assertSuccess(f, await f.download());
  assert.equal(f.requests, 1, 'A valid cache hit must not make a network request.');
});

test('corrupt cached bytes are discarded and replaced with a validated download', options, async t => {
  const f = await fixture(t, servePayload);
  await writeFile(f.target, 'SYNTHETIC_CORRUPT_CACHE');
  await assertSuccess(f, await f.download());
  assert.equal(f.requests, 1);
});

test('selects one executable when multiple curl.exe commands are present on PATH', options, async t => {
  const f = await fixture(t, servePayload);
  const systemRoot = process.env.SystemRoot || process.env.WINDIR;
  assert(systemRoot, 'The Windows fixture requires the system directory.');
  const system32 = resolve(systemRoot, 'System32');
  const duplicate = resolve(f.directory, 'second-curl');
  await mkdir(duplicate);
  await copyFile(resolve(system32, 'curl.exe'), resolve(duplicate, 'curl.exe'));
  const inheritedPath = Object.entries(process.env).find(([name]) => name.toLowerCase() === 'path')?.[1] ?? '';
  // Both entries are real executables. System32 is first so normal curl DLL
  // resolution remains intact; the test does not replace curl with a mock.
  const result = await f.download({}, { PATH: [system32, inheritedPath, duplicate].join(';') });
  await assertSuccess(f, result);
  assert(result.curlApplicationCount >= 2, 'The helper must have seen at least two actual curl application matches.');
  assert.equal(f.requests, 1);
});

test('partial bytes stay unpublished until the complete digest has been verified', options, async t => {
  let release;
  const f = await fixture(t, (_req, res) => {
    res.writeHead(200, { 'Content-Length': payload.length });
    res.write(payload.subarray(0, 64));
    release = () => res.end(payload.subarray(64));
  });
  const pending = f.download({ attemptTimeoutSeconds: 8, stallTimeoutSeconds: 5, maxAttempts: 1 });
  // Retain the promise immediately so an unexpected early failure is handled.
  pending.catch(() => {});
  try {
    await until(async () => release && (await readdir(f.downloads)).some(name => name.includes('.part')), 5000);
    const names = await readdir(f.downloads);
    assert.equal(names.includes('artifact.bin'), false, 'Unverified partial data must never occupy the cache filename.');
    assert.equal(names.filter(name => name.includes('.part')).length, 1);
  } finally { release?.(); }
  await assertSuccess(f, await pending);
});

for (const status of [408, 429, 500, 502, 503, 504]) {
  test(`HTTP ${status} is retried and the eventual body is validated`, options, async t => {
    const f = await fixture(t, (req, res, request) => {
      if (request === 1) { res.writeHead(status); res.end('SYNTHETIC_TRANSIENT_RESPONSE'); }
      else servePayload(req, res);
    });
    await assertSuccess(f, await f.download());
    assert.equal(f.requests, 2, 'Exactly one transient response should produce exactly one retry.');
  });
}

test('an interrupted response body is retried without retaining its partial bytes', options, async t => {
  const f = await fixture(t, (req, res, request, timers) => {
    if (request !== 1) return servePayload(req, res);
    res.writeHead(200, { 'Content-Length': payload.length });
    res.flushHeaders();
    res.write(payload.subarray(0, 1024));
    timers.add(setTimeout(() => res.destroy(), 30));
  });
  await assertSuccess(f, await f.download());
  assert.equal(f.requests, 2);
});

for (const slow of [false, true]) {
  test(`${slow ? 'trickling' : 'stalled'} transfers exhaust a bounded attempt budget and clean up`, options, async t => {
    const f = await fixture(t, (_req, res, _request, timers) => {
      res.writeHead(200, { 'Content-Length': payload.length });
      res.write(payload.subarray(0, 1));
      if (slow) {
        const timer = setInterval(() => res.write(payload.subarray(1, 2)), 150);
        timers.add(timer);
        res.once('close', () => { clearInterval(timer); timers.delete(timer); });
      }
    });
    const result = await f.download({ attemptTimeoutSeconds: 1, maxAttempts: 2 });
    await assertFailure(f, result);
    assert.equal(f.requests, 2, 'Transient timeouts are retried only up to MaxAttempts.');
    assert(result.elapsedMs >= 1000, 'The test must exercise a transfer timeout, not an argument/startup failure.');
    assert(result.elapsedMs <= 6000, `The two one-second attempts exceeded the six-second test budget (${result.elapsedMs} ms).`);
  });
}

test('HTTP 404 is terminal and leaves neither a final file nor partial data', options, async t => {
  const f = await fixture(t, (_req, res) => { res.writeHead(404); res.end('SYNTHETIC_MISSING'); });
  await assertFailure(f, await f.download());
  assert.equal(f.requests, 1, 'A terminal HTTP response must not be retried.');
});

test('a complete hash mismatch is terminal and never enters the cache', options, async t => {
  const f = await fixture(t, (_req, res) => { res.writeHead(200); res.end('SYNTHETIC_WRONG_ARTIFACT'); });
  await assertFailure(f, await f.download());
  assert.equal(f.requests, 1, 'A hash mismatch must not be treated as a transient network problem.');
});

test('HTTP requires explicit loopback-fixture opt-in', options, async t => {
  const f = await fixture(t, servePayload);
  await assertFailure(f, await f.download({ allowLoopbackHttp: false }));
  assert.equal(f.requests, 0);
});

test('loopback opt-in rejects HTTP hosts outside the exact fixture allowlist', options, async t => {
  const f = await fixture(t, servePayload);
  // 127.0.0.2 is not one of the explicitly allowed fixture hosts. Keeping this
  // negative destination within loopback prevents an external network request
  // even if a future regression accidentally starts curl before rejecting it.
  const result = await f.download({ url: f.url.replace('127.0.0.1', '127.0.0.2'), maxAttempts: 1 });
  await assertFailure(f, result);
  assert.match(result.error, /https|loopback|scheme|insecure|http/i, 'Disallowed HTTP must fail URL policy, not a network timeout.');
  assert(result.elapsedMs < 1000, 'Disallowed HTTP must be rejected before any network attempt.');
  assert.equal(f.requests, 0);
});
