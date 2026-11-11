// Local control API for the attendee: HTTP over a unix socket in the work dir (owner-only by the
// directory's permissions). One request = one JSON object in, one JSON object out.
// Actions on the seat are serialised so two commands never fight over the page.
const http = require('http');
const fs = require('fs');

const MAX_WAIT_SECONDS = 600;

function createControlServer({ socketPath, handlers }) {
  if (Buffer.byteLength(socketPath) > 100) throw new Error(`control socket path is too long for a unix socket (${socketPath}); use a shorter --work`);
  fs.rmSync(socketPath, { force: true });
  let chain = Promise.resolve();
  const serial = (fn) => { const run = chain.then(fn, fn); chain = run.catch(() => {}); return run; };

  const server = http.createServer((request, response) => {
    let body = '';
    request.setEncoding('utf8');
    request.on('data', (chunk) => { body += chunk; if (body.length > 64 * 1024) request.destroy(); });
    request.on('end', async () => {
      const reply = (status, value) => { response.writeHead(status, { 'content-type': 'application/json; charset=utf-8' }); response.end(JSON.stringify(value)); };
      const command = (request.url ?? '/').replace(/^\//, '');
      const handler = handlers[command];
      if (request.method !== 'POST' || !handler) { reply(404, { ok: false, error: { source: 'harness', code: 'unknown_command', text: `unknown command ${command}` } }); return; }
      let input;
      try { input = body ? JSON.parse(body) : {}; } catch { reply(400, { ok: false, error: { source: 'harness', code: 'bad_json', text: 'request body is not JSON' } }); return; }
      try {
        const run = () => handler.run(input);
        reply(200, await (handler.serial ? serial(run) : run()));
      } catch (error) {
        reply(500, { ok: false, error: { source: 'harness', code: 'internal', text: error.message } });
      }
    });
  });
  return {
    listen: () => new Promise((resolve, reject) => { server.once('error', reject); server.listen(socketPath, () => { fs.chmodSync(socketPath, 0o600); resolve(); }); }),
    close: () => new Promise((resolve) => { server.close(() => resolve()); server.closeAllConnections?.(); fs.rmSync(socketPath, { force: true }); }),
  };
}

module.exports = { createControlServer, MAX_WAIT_SECONDS };
