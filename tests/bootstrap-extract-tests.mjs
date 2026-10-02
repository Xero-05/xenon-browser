// Real Windows tar/PowerShell regression, using a tiny generated archive only.
// No CEF distribution, browser, downloaded content or existing profile is used.
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { copyFile, mkdir, mkdtemp, readFile, readdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import test from 'node:test';
import { promisify } from 'node:util';

const root = resolve(import.meta.dirname, '..');
const run = promisify(execFile);

test('CEF extraction ignores a shadowed PATH tar and strips the archive wrapper', {
  skip: process.platform !== 'win32', timeout: 25_000,
}, async () => {
  const systemRoot = process.env.SystemRoot || process.env.WINDIR;
  assert(systemRoot, 'The fixture requires the Windows system directory.');
  const system32 = resolve(systemRoot, 'System32');
  const systemTar = resolve(system32, 'tar.exe');
  const cache = resolve(root, '.cache');
  await mkdir(cache, { recursive: true });
  const directory = await mkdtemp(resolve(cache, 'bootstrap-extract-'));
  const source = resolve(directory, 'source');
  const archiveRoot = resolve(source, 'cef_fixture_wrapper');
  const shadow = resolve(directory, 'shadow tar');
  const archive = resolve(directory, 'synthetic cef archive.tar.bz2');
  const destination = resolve(directory, 'extracted files');
  await mkdir(resolve(archiveRoot, 'include'), { recursive: true });
  await mkdir(resolve(archiveRoot, 'Resources', 'locales'), { recursive: true });
  await mkdir(shadow);
  const header = Buffer.from('#define XENON_SYNTHETIC_EXTRACTION_FIXTURE 1\r\n');
  const resource = Buffer.from([0, 1, 2, 3, 127, 128, 254, 255]);
  await writeFile(resolve(archiveRoot, 'include', 'fixture.h'), header);
  await writeFile(resolve(archiveRoot, 'Resources', 'locales', 'fixture.bin'), resource);
  await run(systemTar, ['-cjf', archive, '-C', source, 'cef_fixture_wrapper'], { windowsHide: true, timeout: 10_000 });
  // This is a real Windows executable, but it cannot extract an archive. If
  // the helper regresses to PATH discovery, extraction must fail this test.
  const shadowTar = resolve(shadow, 'tar.exe');
  await copyFile(resolve(system32, 'where.exe'), shadowTar);
  const wrapper = resolve(directory, 'invoke.ps1');
  await writeFile(wrapper, String.raw`param([string]$HelperPath, [string]$ArchivePath, [string]$DestinationPath)
$ErrorActionPreference = 'Stop'
$taskSelectedByPath = (Get-Command tar.exe -CommandType Application | Select-Object -First 1).Source
. $HelperPath
Expand-XenonCefArchive -Archive $ArchivePath -Destination $DestinationPath -TimeoutSeconds 5
[Console]::Out.WriteLine('XENON_EXTRACT_TEST_RESULT:' + (@{ pathTar=$taskSelectedByPath } | ConvertTo-Json -Compress))
`, 'utf8');
  const environment = { ...process.env };
  const inheritedPath = Object.entries(environment).find(([name]) => name.toLowerCase() === 'path')?.[1] ?? '';
  for (const name of Object.keys(environment)) if (name.toLowerCase() === 'path') delete environment[name];
  environment.PATH = `${shadow};${inheritedPath}`;
  const { stdout } = await run(process.env.XENON_TEST_POWERSHELL || 'pwsh.exe', [
    '-NoProfile', '-NonInteractive', '-File', wrapper,
    '-HelperPath', resolve(root, 'scripts/dependency-extract.ps1'),
    '-ArchivePath', archive, '-DestinationPath', destination,
  ], { cwd: root, env: environment, windowsHide: true, timeout: 12_000 });
  const marker = 'XENON_EXTRACT_TEST_RESULT:';
  const results = stdout.split(/\r?\n/u).filter(line => line.startsWith(marker));
  assert.equal(results.length, 1, 'The PowerShell helper must complete normally.');
  const result = JSON.parse(results[0].slice(marker.length));
  assert.equal(result.pathTar.toLowerCase(), shadowTar.toLowerCase(), 'The child PATH must really select the non-extractor tar.exe.');
  assert.deepEqual((await readdir(destination)).sort(), ['Resources', 'include'], 'Exactly one archive wrapper must be stripped.');
  assert.deepEqual(await readFile(resolve(destination, 'include', 'fixture.h')), header);
  assert.deepEqual(await readFile(resolve(destination, 'Resources', 'locales', 'fixture.bin')), resource);
});
