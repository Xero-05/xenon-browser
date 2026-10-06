import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { open } from 'node:fs/promises';
import { win32 } from 'node:path';
import { BrokerError, type ClientConfig } from './ipc.js';
const run = promisify(execFile);

// Windows searches the current directory before PATH for a bare executable
// name, so a planted icacls.exe could skip the ACL. Use absolute System32 paths.
export function systemToolPath(name: string, systemRoot = process.env.SystemRoot): string {
  return win32.join(systemRoot && win32.isAbsolute(systemRoot) ? systemRoot : 'C:\\Windows', 'System32', name);
}

// Windows ignores POSIX file modes. Apply the current user's protected DACL to
// an empty, exclusively-created file BEFORE writing the pairing token.
export async function writePrivateConfig(path: string, config: ClientConfig): Promise<void> {
  await writePrivateFile(path, JSON.stringify(config, null, 2) + '\n');
}

export async function writePrivateFile(path: string, data: string | Uint8Array): Promise<void> {
  const file = await open(path, 'wx', 0o600);
  try {
    await restrictPrivatePath(path);
    await file.writeFile(data);
    await file.sync();
  } finally { await file.close(); }
}

export async function restrictPrivatePath(path: string, directory = false): Promise<void> {
  if (process.platform !== 'win32') return;
  const result = await run(systemToolPath('whoami.exe'), ['/user', '/fo', 'csv', '/nh'], { windowsHide: true });
  const sid = result.stdout.match(/S-1-\d+(?:-\d+)+/)?.[0];
  if (!sid) throw new BrokerError('ACL_FAILED', 'Could not identify the current Windows user.');
  await run(systemToolPath('icacls.exe'), [path, '/inheritance:r', '/grant:r', `*${sid}:${directory ? '(OI)(CI)' : ''}(F)`], { windowsHide: true });
}
