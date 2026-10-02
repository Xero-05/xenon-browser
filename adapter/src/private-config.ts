import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { open } from 'node:fs/promises';
import { BrokerError, type ClientConfig } from './ipc.js';
const run = promisify(execFile);

// Windows ignores POSIX file modes. Apply the current user's protected DACL to
// an empty, exclusively-created file BEFORE writing the pairing token.
export async function writePrivateConfig(path: string, config: ClientConfig): Promise<void> {
  const file = await open(path, 'wx', 0o600);
  try {
    if (process.platform === 'win32') {
      const result = await run('whoami.exe', ['/user', '/fo', 'csv', '/nh'], { windowsHide: true });
      const sid = result.stdout.match(/S-1-\d+(?:-\d+)+/)?.[0];
      if (!sid) throw new BrokerError('ACL_FAILED', 'Could not identify the current Windows user.');
      await run('icacls.exe', [path, '/inheritance:r', '/grant:r', `*${sid}:(F)`], { windowsHide: true });
    }
    await file.writeFile(JSON.stringify(config, null, 2) + '\n', 'utf8');
    await file.sync();
  } finally { await file.close(); }
}
