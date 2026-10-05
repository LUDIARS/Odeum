// @implements SPEC-VANMAC-BROADCAST
import { constants } from 'node:fs';
import { copyFile, lstat, mkdir, readFile, rm } from 'node:fs/promises';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { homedir } from 'node:os';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../', import.meta.url));
async function install() {
  if (process.platform !== 'darwin' || process.argv.length !== 2) throw new Error('Run without arguments on the macOS parent');
  try {
    await promisify(execFile)('/usr/bin/pgrep', ['-x', 'OBS']);
    throw new Error('Close OBS before installing');
  } catch (error) { if (error.code !== 1) throw error; }
  const source = resolve(root, 'artifacts/broadcast');
  const receipt = JSON.parse(await readFile(resolve(source, 'prepared.json'), 'utf8'));
  if (receipt.version !== 1 || receipt.service !== 'odeum-broadcast') throw new Error('Preparation is incomplete');
  for (const name of ['Odeum_VANMAC.json', 'basic.ini', 'service.json', 'streamEncoder.json']) {
    if (!(await lstat(resolve(source, name))).isFile()) throw new Error('Expected regular configuration files');
  }
  const basic = resolve(homedir(), 'Library/Application Support/obs-studio/basic');
  const profile = resolve(basic, 'profiles/Odeum_VANMAC');
  const scene = resolve(basic, 'scenes/Odeum_VANMAC.json');
  await mkdir(resolve(basic, 'profiles'), { recursive: true });
  await mkdir(resolve(basic, 'scenes'), { recursive: true });
  await mkdir(profile, { mode: 0o700 }); // Existing profile is never replaced.
  let sceneCreated = false;
  try {
    for (const name of ['basic.ini', 'service.json', 'streamEncoder.json'])
      await copyFile(resolve(source, name), resolve(profile, name), constants.COPYFILE_EXCL);
    await copyFile(resolve(source, 'Odeum_VANMAC.json'), scene, constants.COPYFILE_EXCL);
    sceneCreated = true;
    process.stdout.write('Installed OBS collection/profile Odeum_VANMAC. OBS remains stopped.\n');
  } catch (error) {
    // Only this invocation's new profile and scene may be rolled back.
    await rm(profile, { recursive: true });
    if (sceneCreated) await rm(scene);
    throw error;
  }
}
install().catch(error => { process.stderr.write(`OBS configuration installation failed: ${error.code ?? error.message}\n`); process.exitCode = 1; });
