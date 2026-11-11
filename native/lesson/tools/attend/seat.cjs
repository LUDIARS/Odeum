#!/usr/bin/env node
// The attendee's seat: a thin client of the session's control API. One command per call; the
// answer is JSON on stdout. Exit status 0 = ok, 1 = the page or the harness refused (see .error),
// 2 = no session / bad usage.
//
//   ATTEND_WORK=<dir> node seat.cjs observe [--all]
//   ATTEND_WORK=<dir> node seat.cjs wait --for change --timeout 30
//   ATTEND_WORK=<dir> node seat.cjs answer --text "..."      |  answer --choices 0,2
//   ATTEND_WORK=<dir> node seat.cjs tick --step 2 [--undo]
//   ATTEND_WORK=<dir> node seat.cjs help
//   ATTEND_WORK=<dir> node seat.cjs ask --text "..." [--show-name]
//   ATTEND_WORK=<dir> node seat.cjs say --text "..."
//   ATTEND_WORK=<dir> node seat.cjs vote --n 1
//   ATTEND_WORK=<dir> node seat.cjs tool-link
//   ATTEND_WORK=<dir> node seat.cjs unresolved --n 1      (「まだ分からない」 on your own question)
//   ATTEND_WORK=<dir> node seat.cjs status
//   ATTEND_WORK=<dir> node seat.cjs stop
const http = require('http');
const path = require('path');
const fs = require('fs');
const { parseArgs } = require('./lib/args.cjs');

const COMMANDS = {
  observe: { flags: ['all'], body: (o) => ({ all: Boolean(o.all) }) },
  wait: { allow: ['for', 'timeout'], flags: ['all'], body: (o) => ({ for: o.for ?? 'change', timeout: Number(o.timeout ?? 30), all: Boolean(o.all) }) },
  answer: {
    allow: ['text', 'choices'],
    body: (o) => {
      if ((o.text === undefined) === (o.choices === undefined)) throw new Error('answer needs exactly one of --text or --choices');
      if (o.text !== undefined) return { text: o.text };
      const choices = String(o.choices).split(',').map((value) => Number(value.trim()));
      if (choices.some((value) => !Number.isInteger(value))) throw new Error('--choices is a comma separated list of 0-based numbers');
      return { choices };
    },
  },
  tick: { allow: ['step'], flags: ['undo'], body: (o) => { const step = Number(o.step); if (!Number.isInteger(step)) throw new Error('tick needs --step N (1-based, as numbered on the page)'); return { step, undo: Boolean(o.undo) }; } },
  help: { body: () => ({}) },
  ask: { allow: ['text'], flags: ['show-name'], body: (o) => { if (!o.text) throw new Error('ask needs --text'); return { text: o.text, showName: Boolean(o['show-name']) }; } },
  say: { allow: ['text'], body: (o) => { if (!o.text) throw new Error('say needs --text'); return { text: o.text }; } },
  vote: { allow: ['n', 'id'], body: (o) => { const n = Number(o.n ?? o.id); if (!Number.isInteger(n)) throw new Error('vote needs --n N (the shared question\'s n from observe)'); return { n }; } },
  'tool-link': { body: () => ({}) },
  unresolved: { allow: ['n'], body: (o) => { const n = Number(o.n); if (!Number.isInteger(n)) throw new Error('unresolved needs --n N (your question\'s n from observe my_questions)'); return { n }; } },
  status: { body: () => ({}) },
  stop: { body: () => ({}) },
};

function usage(message) {
  process.stdout.write(`${JSON.stringify({ ok: false, error: { source: 'seat', code: 'usage', text: message }, commands: Object.keys(COMMANDS) }, null, 2)}\n`);
  process.exit(2);
}

function main() {
  const argv = process.argv.slice(2);
  const globalWork = argv.indexOf('--work');
  let work = process.env.ATTEND_WORK;
  if (globalWork >= 0) { work = argv[globalWork + 1]; argv.splice(globalWork, 2); }
  const [command, ...rest] = argv;
  const spec = COMMANDS[command];
  if (!spec) usage(`unknown command ${command ?? '(none)'}`);
  if (!work) usage('set ATTEND_WORK or pass --work <dir> (the session runner prints it)');
  let body;
  try {
    const { options } = parseArgs(rest, { flags: spec.flags ?? [], allow: spec.allow ?? [] });
    body = spec.body(options);
  } catch (error) { usage(error.message); }
  const socketPath = path.join(path.resolve(work), 'seat.sock');
  if (!fs.existsSync(socketPath)) usage(`no session is listening in ${work} (no seat.sock)`);
  const payload = JSON.stringify(body);
  const timeout = command === 'wait' ? (body.timeout + 30) * 1000 : 120000;
  const request = http.request({ socketPath, path: `/${command}`, method: 'POST', headers: { 'content-type': 'application/json', 'content-length': Buffer.byteLength(payload) }, timeout }, (response) => {
    let text = '';
    response.setEncoding('utf8');
    response.on('data', (chunk) => { text += chunk; });
    response.on('end', () => {
      let value;
      try { value = JSON.parse(text); } catch { usage(`session answered with non-JSON: ${text.slice(0, 200)}`); }
      process.stdout.write(`${JSON.stringify(value, null, 2)}\n`);
      process.exit(value.ok ? 0 : 1);
    });
  });
  request.on('timeout', () => { request.destroy(); usage('the session did not answer in time'); });
  request.on('error', (error) => usage(`cannot reach the session: ${error.message}`));
  request.end(payload);
}

main();
