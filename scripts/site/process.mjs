import { spawn } from 'node:child_process';
import { mkdir, appendFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

export const root = fileURLToPath(new URL('../../', import.meta.url));

export function childEnvironment() {
  // Windows MSBuild rejects duplicate Path/PATH entries inherited from some launchers.
  const env = {};
  for (const [key, value] of Object.entries(process.env)) {
    if (process.platform === 'win32' && key.toLowerCase() === 'path') continue;
    env[key] = value;
  }
  if (process.platform === 'win32') env.Path = process.env.Path ?? process.env.PATH;
  return env;
}

export async function runBuild(args) {
  await mkdir(resolve(root, '.vestigium-logs'), { recursive: true });
  return new Promise((resolveRun, reject) => {
    const child = spawn('cmake', args, { cwd: root, env: childEnvironment(), shell: false, detached: process.platform !== 'win32', windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let log = Promise.resolve();
    let logError;
    const record = chunk => { log = log.then(() => appendFile(resolve(root, '.vestigium-logs/setup.log'), chunk)).catch(error => { logError = error; }); };
    child.stdout.on('data', record);
    child.stderr.on('data', record);
    let termination = Promise.resolve();
    const timer = setTimeout(() => {
      if (!child.pid) return;
      if (process.platform === 'win32') {
        termination = new Promise((done, fail) => {
          const killer = spawn('taskkill.exe', ['/PID', String(child.pid), '/T', '/F'], { shell: false, windowsHide: true, stdio: 'ignore' });
          killer.once('error', fail); killer.once('close', code => code === 0 ? done() : fail(new Error('Build process cleanup failed')));
        });
        termination.catch(() => { /* Observed again on child close; avoid an unhandled promise in the interim. */ });
      } else {
        try { process.kill(-child.pid, 'SIGKILL'); }
        catch (error) { if (error.code !== 'ESRCH') logError = error; }
      }
    }, 7 * 60 * 1000);
    child.once('error', error => { clearTimeout(timer); reject(error); });
    child.once('close', async code => {
      clearTimeout(timer);
      try { await termination; await log; if (logError) throw logError; if (code !== 0) throw new Error('CMake failed; see .vestigium-logs/setup.log'); resolveRun(); }
      catch (error) { reject(error); }
    });
  });
}
