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
  // odeum-program is built from the sibling Tela and Pictor checkouts (override with the env vars).
  const ars = resolve(root, '..');
  const tela = process.env.ODEUM_TELA_SOURCE_DIR ?? resolve(ars, 'Tela');
  const pictorInclude = process.env.TELA_PICTOR_INCLUDE_DIR ?? resolve(ars, 'Pictor/include');
  const pictorLibrary = process.env.TELA_PICTOR_LIBRARY
    ?? resolve(ars, process.platform === 'win32' ? 'Pictor/build/Release/pictor.lib' : 'Pictor/build/libpictor.a');
  for (const path of [tela, pictorInclude, pictorLibrary]) await access(path);
  await runBuild(['-S', 'native', '-B', 'native/build', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON',
    '-DODEUM_BUILD_PROGRAM=ON', `-DODEUM_TELA_SOURCE_DIR=${tela}`, `-DTELA_PICTOR_INCLUDE_DIR=${pictorInclude}`,
    `-DTELA_PICTOR_LIBRARY=${pictorLibrary}`]);
  await runBuild(['--build', 'native/build', '--config', 'Release', '--parallel', '2']);
  const executable = name => resolve(root, 'native/build/bin', process.platform === 'win32' ? `${name}.exe` : name);
  await access(executable('odeum-relay'));
  await access(executable('odeum-program'));
} catch {
  process.stderr.write('odeum-relay/odeum-program setup failed; verify public key configuration, C++20 compiler, OpenSSL 3, Tela/Pictor checkouts and setup log\n');
  process.exitCode = 1;
}
