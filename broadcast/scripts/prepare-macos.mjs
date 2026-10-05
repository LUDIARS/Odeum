// @implements SPEC-VANMAC-BROADCAST
import { access, copyFile, lstat, mkdir, readFile, writeFile, chmod } from 'node:fs/promises';
import { constants } from 'node:fs';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { randomBytes, randomUUID } from 'node:crypto';
import { isAbsolute, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { isIP } from 'node:net';
import { networkInterfaces } from 'node:os';
import { relayConfiguration } from './relay-config.mjs';
import { macSceneCollection, macProfile, streamEncoder } from './obs-config.mjs';

const root = fileURLToPath(new URL('../../', import.meta.url));
const execute = promisify(execFile);

async function prepare() {
  const args = process.argv.slice(2);
  if (args.length !== 4 || args[0] !== '--parent-ip' || args[2] !== '--mediamtx')
    throw new Error('Usage: node broadcast/scripts/prepare-macos.mjs --parent-ip <Tailscale IPv4> --mediamtx <absolute MediaMTX 1.21.1 binary>');
  if (process.platform !== 'darwin') throw new Error('Run preparation on the macOS parent itself');
  const host = args[1], binary = args[3], octets = host.split('.').map(Number);
  if (isIP(host) !== 4 || octets[0] !== 100 || octets[1] < 64 || octets[1] > 127)
    throw new Error('An explicit Tailscale IPv4 address is required');
  if (!Object.values(networkInterfaces()).flat().some(item => item?.address === host))
    throw new Error('Parent IP is not assigned to this computer');
  if (!isAbsolute(binary) || !(await lstat(binary)).isFile()) throw new Error('MediaMTX must be an absolute regular executable');
  await access(binary, constants.X_OK);
  await access('/Applications/OBS.app/Contents/MacOS/OBS', constants.X_OK);
  const git = await lstat(resolve(root, '.git'));
  const branch = await execute('git', ['branch', '--show-current'], { cwd: root });
  if (!git.isDirectory() || branch.stdout.trim() !== 'main') throw new Error('Use the main checkout, not a worktree');
  const catalog = await readFile(resolve(root, 'excubitor.catalog.yaml'), 'utf8');
  const entry = catalog.split(/\r?\n(?=  - code:)/).find(part => /- code: odeum-broadcast\s/.test(part));
  const ports = entry?.match(/^    port: (\d+)$/gm);
  if (ports?.length !== 1) throw new Error('Missing unambiguous broadcast catalog port');
  const port = Number(ports[0].split(':')[1]);
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Invalid catalog port');
  const data = relayConfiguration(host, port, () => randomBytes(24).toString('hex'));
  const template = JSON.parse(await readFile(resolve(root, 'broadcast/obs/Odeum_YouTube_Program.json'), 'utf8'));
  const profile = await readFile(resolve(root, 'broadcast/obs/profile/basic.ini'), 'utf8');
  const destination = resolve(root, 'artifacts/broadcast');
  await mkdir(resolve(root, 'artifacts'), { recursive: true });
  // Exclusive private directory: retries never rotate credentials or overwrite a previous preparation.
  await mkdir(destination, { mode: 0o700 });
  const save = (name, value) => writeFile(resolve(destination, name), value, { flag: 'wx', mode: 0o600 });
  const json = value => JSON.stringify(value, null, 2) + '\n';
  await save('mediamtx.yml', json(data.configuration)); // JSON is a YAML subset accepted by MediaMTX.
  await save('Odeum_VANMAC.json', json(macSceneCollection(template, data.parentInputs, randomUUID)));
  await save('basic.ini', macProfile(profile, data.parentOutput));
  await save('streamEncoder.json', json(streamEncoder));
  await save('service.json', await readFile(resolve(root, 'broadcast/obs/profile/service.json')));
  for (const client of data.clients) await save(`sender-${client.input}.json`, json(client));
  await copyFile(binary, resolve(destination, 'mediamtx'), constants.COPYFILE_EXCL);
  await chmod(resolve(destination, 'mediamtx'), 0o700);
  // Written last; an interrupted preparation cannot be installed as a complete bundle.
  await save('prepared.json', json({ version: 1, service: 'odeum-broadcast', host, port, mediaMtxVersion: '1.21.1' }));
  process.stdout.write('Prepared private broadcast files in artifacts/broadcast. Nothing was started.\n');
}

prepare().catch(error => {
  // Do not print network URLs or generated contents. Native errors may include file paths only.
  process.stderr.write(`Broadcast preparation failed: ${error.code ?? error.message}\n`);
  process.exitCode = 1;
});
