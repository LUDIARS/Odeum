// @implements SPEC-VANMAC-BROADCAST
import { lstat, readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { homedir } from 'node:os';

export async function exists(path) {
  try { await lstat(path); return true; }
  catch (error) { if (error.code === 'ENOENT') return false; throw error; }
}
export async function regular(path) {
  const stat = await lstat(path);
  if (!stat.isFile() || stat.isSymbolicLink()) throw new Error('Expected a regular broadcast file');
}
export async function validatePreparation(root, host) {
  const directory = resolve(root, 'artifacts/broadcast');
  const stat = await lstat(directory);
  if (!stat.isDirectory() || stat.isSymbolicLink()) throw new Error('Invalid broadcast directory');
  await regular(resolve(directory, 'prepared.json'));
  const receipt = JSON.parse(await readFile(resolve(directory, 'prepared.json'), 'utf8'));
  if (receipt.version !== 1 || receipt.service !== 'odeum-broadcast' || receipt.host !== host || receipt.mediaMtxVersion !== '1.21.1')
    throw new Error('Existing preparation does not match this host; manual reconciliation required');
  for (const name of ['mediamtx', 'mediamtx.yml', 'Odeum_VANMAC.json', 'basic.ini', 'service.json', 'streamEncoder.json',
    'sender-1.json', 'sender-2.json', 'sender-3.json', 'sender-4.json']) await regular(resolve(directory, name));
}
export async function obsConfigurationInstalled(root) {
  const basic = resolve(homedir(), 'Library/Application Support/obs-studio/basic');
  const profile = resolve(basic, 'profiles/Odeum_VANMAC');
  const scene = resolve(basic, 'scenes/Odeum_VANMAC.json');
  if (!await exists(profile) && !await exists(scene)) return false;
  const stat = await lstat(profile);
  if (!stat.isDirectory() || stat.isSymbolicLink()) throw new Error('Invalid OBS profile directory');
  // A retry can adopt an identical installation; user edits are never overwritten.
  for (const name of ['basic.ini', 'service.json', 'streamEncoder.json', 'Odeum_VANMAC.json']) {
    const target = name === 'Odeum_VANMAC.json' ? scene : resolve(profile, name);
    await regular(target);
    if (!(await readFile(target)).equals(await readFile(resolve(root, 'artifacts/broadcast', name))))
      throw new Error('Existing OBS configuration differs; manual reconciliation required');
  }
  return true;
}
