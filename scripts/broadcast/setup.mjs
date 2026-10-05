// @implements SPEC-VANMAC-BROADCAST
import { access, lstat, mkdir, mkdtemp, open, rm } from 'node:fs/promises';
import { constants } from 'node:fs';
import { networkInterfaces, userInfo } from 'node:os';
import { resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { downloadMediaMtx } from './download-mediamtx.mjs';
import { exists, validatePreparation, obsConfigurationInstalled } from './state.mjs';

const root = fileURLToPath(new URL('../../', import.meta.url));
const execute = promisify(execFile);
async function setup() {
  if (process.platform !== 'darwin' || process.argv.length !== 2) throw new Error('Setup requires macOS and no arguments');
  if (userInfo().uid === 0) throw new Error('Run Ex as the OBS desktop user, not root');
  if (!(await lstat(resolve(root, '.git'))).isDirectory() ||
      (await execute('git', ['branch', '--show-current'], { cwd: root })).stdout.trim() !== 'main')
    throw new Error('Setup requires the main project checkout');
  await access('/Applications/OBS.app/Contents/MacOS/OBS', constants.X_OK);
  const consoleUser = (await execute('/usr/bin/stat', ['-f', '%Su', '/dev/console'], { cwd: root })).stdout.trim();
  if (consoleUser !== userInfo().username) throw new Error('Ex account must be the logged-in OBS desktop user');
  try {
    await execute('/usr/bin/pgrep', ['-x', 'OBS'], { cwd: root });
    throw new Error('Close OBS before setup');
  } catch (error) { if (error.code !== 1) throw error; }
  const hosts = [...new Set(Object.values(networkInterfaces()).flat().filter(item => {
    const parts = item?.address.split('.').map(Number);
    return item?.family === 'IPv4' && parts[0] === 100 && parts[1] >= 64 && parts[1] <= 127;
  }).map(item => item.address))];
  if (hosts.length !== 1) throw new Error('Exactly one local Tailscale IPv4 address is required');
  const artifacts = resolve(root, 'artifacts');
  await mkdir(artifacts, { recursive: true });
  if ((await lstat(artifacts)).isSymbolicLink()) throw new Error('Artifacts must not be a symlink');
  const lockPath = join(artifacts, 'broadcast-bootstrap.lock');
  const lock = await open(lockPath, 'wx', 0o600);
  let temporary;
  try {
    if (!await exists(join(artifacts, 'broadcast'))) {
      temporary = await mkdtemp(join(artifacts, 'broadcast-download-'));
      const binary = await downloadMediaMtx(temporary);
      await execute(process.execPath, ['broadcast/scripts/prepare-macos.mjs', '--parent-ip', hosts[0], '--mediamtx', binary], { cwd: root, timeout: 120_000 });
    }
    await validatePreparation(root, hosts[0]);
    if (!await obsConfigurationInstalled(root))
      await execute(process.execPath, ['broadcast/scripts/install-obs-macos.mjs'], { cwd: root, timeout: 30_000 });
  } finally {
    try { if (temporary) await rm(temporary, { recursive: true }); }
    finally { await lock.close(); await rm(lockPath); }
  }
}
setup().catch(error => {
  // Child output can contain private settings; report only our messages or native error codes.
  process.stderr.write(`Broadcast setup failed: ${error.code ?? error.message}\n`);
  process.exitCode = 1;
});
