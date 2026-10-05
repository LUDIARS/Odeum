// @implements SPEC-VANMAC-BROADCAST
import { createHash } from 'node:crypto';
import { lstat, readFile, writeFile } from 'node:fs/promises';
import { isAbsolute } from 'node:path';

// The router persists no program/media data. Host-bound credentials and OBS edits
// are deliberately not portable; the destination provisions its own identities.
const envelope = { version: 1, service: 'odeum-broadcast', policy: 'fresh-host-identities', data: {} };
export async function exportData(args) {
  if (args.length !== 2 || args[0] !== '--output' || !isAbsolute(args[1])) throw new Error('Expected --output absolute-path');
  await writeFile(args[1], JSON.stringify(envelope) + '\n', { flag: 'wx', mode: 0o600 });
}
export async function importData(args) {
  if (args.length !== 4 || args[0] !== '--input' || args[2] !== '--sha256' || !isAbsolute(args[1]) || !/^[a-f0-9]{64}$/.test(args[3]))
    throw new Error('Expected --input absolute-path --sha256 digest');
  const stat = await lstat(args[1]);
  if (!stat.isFile() || stat.isSymbolicLink() || stat.size > 4096) throw new Error('Invalid broadcast data bundle');
  const bytes = await readFile(args[1]);
  if (createHash('sha256').update(bytes).digest('hex') !== args[3]) throw new Error('Broadcast data checksum mismatch');
  const value = JSON.parse(bytes.toString('utf8'));
  if (value?.version !== envelope.version || value.service !== envelope.service || value.policy !== envelope.policy ||
      !value.data || Array.isArray(value.data) || typeof value.data !== 'object' || Object.keys(value.data).length !== 0 ||
      Object.keys(value).sort().join(',') !== 'data,policy,service,version')
    throw new Error('Unsupported broadcast data bundle');
  // Valid empty imports have no persistent data to install and never touch local identities.
}
