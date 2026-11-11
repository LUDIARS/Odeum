// Starts odeum-lesson for one session and stops it again.
// The console token is written by the service to a file in the work dir and read back the same way
// e2e.cjs does; it is never printed.
const { spawn, execFileSync } = require('child_process');
const fs = require('fs');
const net = require('net');
const http = require('http');
const path = require('path');

const STUDENT_PORT = 4410;
const CONSOLE_PORT = 4411;
const DEFAULT_BIN = path.resolve(__dirname, '../../../../build/bin/odeum-lesson');

function assertPortFree(port) {
  return new Promise((resolve, reject) => {
    const probe = net.createServer();
    probe.once('error', (error) => reject(new Error(`port ${port} on 127.0.0.1 is busy (${error.code}); stop whatever is listening there first`)));
    probe.listen(port, '127.0.0.1', () => probe.close(resolve));
  });
}

function healthy(port) {
  return new Promise((resolve) => {
    const request = http.get({ host: '127.0.0.1', port, path: '/health', timeout: 1000 }, (response) => {
      response.resume();
      resolve(response.statusCode === 200);
    });
    request.on('error', () => resolve(false));
    request.on('timeout', () => { request.destroy(); resolve(false); });
  });
}

async function startService({ bin = DEFAULT_BIN, work, plot }) {
  if (!fs.existsSync(bin)) throw new Error(`odeum-lesson not found at ${bin}; build it with cmake --build native/build`);
  await assertPortFree(STUDENT_PORT);
  await assertPortFree(CONSOLE_PORT);
  const tokenFile = path.join(work, 'console.token');
  fs.rmSync(tokenFile, { force: true });
  const stderr = fs.openSync(path.join(work, 'odeum-lesson.stderr.log'), 'w');
  const env = { ...process.env, ODEUM_LESSON_CONSOLE_TOKEN_FILE: tokenFile };
  if (plot) env.ODEUM_LESSON_PLOT = path.resolve(plot);
  const child = spawn(bin, [], { env, stdio: ['ignore', stderr, stderr] });
  let exited = null;
  child.on('exit', (code, signal) => { exited = { code, signal }; });
  const deadline = Date.now() + 15000;
  for (;;) {
    if (exited) throw new Error(`odeum-lesson exited during start-up (${JSON.stringify(exited)}); see ${path.join(work, 'odeum-lesson.stderr.log')}`);
    if (fs.existsSync(tokenFile) && await healthy(STUDENT_PORT) && await healthy(CONSOLE_PORT)) break;
    if (Date.now() > deadline) { child.kill('SIGTERM'); throw new Error('odeum-lesson did not become healthy on 4410/4411 within 15 s'); }
    await new Promise((resolve) => setTimeout(resolve, 200));
  }
  const token = fs.readFileSync(tokenFile, 'utf8').trim();
  if (!token) throw new Error(`console token file ${tokenFile} is empty`);
  return {
    token,
    studentUrl: `http://127.0.0.1:${STUDENT_PORT}`,
    consoleUrl: `http://127.0.0.1:${CONSOLE_PORT}`,
    exited: () => exited,
    stop: () => new Promise((resolve) => {
      if (exited) { resolve(); return; }
      child.once('exit', () => resolve());
      child.kill('SIGTERM');
      setTimeout(() => { if (!exited) child.kill('SIGKILL'); }, 5000);
    }),
  };
}

// The timeline the service will play, from the service's own CLI.
function loadTimeline({ bin = DEFAULT_BIN, course, plot }) {
  const env = { ...process.env };
  if (plot) env.ODEUM_LESSON_PLOT = path.resolve(plot);
  const target = plot ? path.resolve(plot) : course;
  return JSON.parse(execFileSync(bin, ['timeline', target], { env, encoding: 'utf8' }));
}

module.exports = { startService, loadTimeline, DEFAULT_BIN };
