import { readFile, access } from 'node:fs/promises';
import { createPublicKey } from 'node:crypto';
import { resolve } from 'node:path';
import { root, runBuild } from './process.mjs';

try {
  const path = process.env.ODEUM_RELAY_TICKET_PUBKEYS;
  if (!path) throw new Error('ODEUM_RELAY_TICKET_PUBKEYS is required');
  const keys = JSON.parse(await readFile(path, 'utf8'));
  if (!keys || Array.isArray(keys) || Object.keys(keys).length === 0) throw new Error('Public key map must not be empty');
  for (const [kid, pem] of Object.entries(keys)) {
    if (!kid || typeof pem !== 'string' || createPublicKey(pem).asymmetricKeyType !== 'ed25519') throw new Error('Ed25519 public keys required');
  }
  await runBuild(['-S', 'native', '-B', 'native/build', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON']);
  await runBuild(['--build', 'native/build', '--config', 'Release', '--parallel', '2']);
  await access(resolve(root, 'native/build/bin', process.platform === 'win32' ? 'odeum-relay.exe' : 'odeum-relay'));
} catch {
  process.stderr.write('odeum-relay setup failed; verify public key configuration, C++20 compiler, OpenSSL 3 and setup log\n');
  process.exitCode = 1;
}
