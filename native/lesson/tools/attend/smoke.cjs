#!/usr/bin/env node
// Smoke test of the harness with a trivial scripted attendee (not an AI): starts classroom.cjs,
// follows the class only through seat.cjs, answers every question with canned text, works the
// assignment, asks for a hint, asks a question, says something, votes, and stops at the end.
// Writes <work>/smoke-report.json and exits 1 if one of the checks failed.
//   NO_PROXY=127.0.0.1,localhost node native/lesson/tools/attend/smoke.cjs --work <dir> [classroom options...]
const { spawn, execFile } = require('child_process');
const fs = require('fs');
const path = require('path');

const argv = process.argv.slice(2);
const workAt = argv.indexOf('--work');
if (workAt < 0 || !argv[workAt + 1]) { console.error('usage: smoke.cjs --work <dir> [classroom options]'); process.exit(2); }
const work = path.resolve(argv[workAt + 1]);
const seatCli = path.join(__dirname, 'seat.cjs');
const began = Date.now();

function seat(command, ...args) {
  return new Promise((resolve, reject) => {
    execFile(process.execPath, [seatCli, command, ...args], { env: { ...process.env, ATTEND_WORK: work }, maxBuffer: 16 << 20 }, (error, stdout) => {
      if (error && error.code === 2) { reject(new Error(`seat ${command}: ${stdout}`)); return; }
      try { resolve(JSON.parse(stdout)); } catch { reject(new Error(`seat ${command}: not JSON: ${stdout}`)); }
    });
  });
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const say = (...words) => console.log(`[smoke ${Math.round((Date.now() - began) / 1000)}s]`, ...words);

const QUESTION = 'ヒットストップは何秒にするのがいいですか？なぜですか';
const TEXT = 'ヒットストップで一瞬止める、ノックバックで押し返す、カメラを揺らす。威力で止める長さを変え、UI は止めない。壁際や連続ヒットの扱いも決める。';

async function attend(report) {
  const answered = new Set();
  let view = await seat('observe');
  let asked = false, spoke = false, voted = false, helped = false, lastTick = 0;
  while (view.lesson_state !== 'ended' && view.lesson_state !== 'closed') {
    for (const message of view.private_new ?? []) report.private.push(message);
    const activity = view.activity;
    if (activity?.state === 'open' && activity.id && !answered.has(activity.id)) {
      answered.add(activity.id);
      const multi = activity.input.includes('multi');
      const result = activity.choices ? await seat('answer', '--choices', multi ? '1,2' : '1') : await seat('answer', '--text', TEXT);
      report.answers[activity.id] = { ok: result.ok, acked: result.acked ?? false, error: result.error ?? null };
      say('answer', activity.id, result.ok ? 'acked' : JSON.stringify(result.error));
      view = result.observe;
      continue;
    }
    if (activity?.id && activity.personal_feedback) report.feedback[activity.id] = activity.personal_feedback;
    if (view.assignment?.active && Date.now() - lastTick > 12000) {
      const next = view.assignment.steps.find((step) => !step.done);
      if (next) {
        lastTick = Date.now();
        const result = await seat('tick', '--step', String(next.n));
        report.ticks.push({ n: next.n, ok: result.ok, error: result.error ?? null });
        say('tick', next.n, result.ok ? 'ok' : JSON.stringify(result.error));
        if (!helped && next.n === 2) {
          helped = true;
          const hint = await seat('help');
          report.help = { ok: hint.ok, private: hint.observe.private_new };
          for (const message of hint.observe.private_new) report.private.push(message);
          say('help', hint.ok ? JSON.stringify(hint.observe.private_new.map((m) => m.text)) : JSON.stringify(hint.error));
        }
        view = (await seat('observe'));
        continue;
      }
    }
    if (!asked && view.section?.index >= 4) {
      asked = true;
      const result = await seat('ask', '--text', QUESTION);
      report.question = { ok: result.ok, private: result.observe.private_new, error: result.error ?? null };
      for (const message of result.observe.private_new) report.private.push(message);
      say('ask', result.ok ? JSON.stringify(result.observe.private_new.map((m) => m.text)) : JSON.stringify(result.error));
      view = result.observe;
      continue;
    }
    if (!spoke && view.section?.index >= 5) {
      spoke = true;
      const result = await seat('say', '--text', 'ノックバックの向きが気になりました');
      report.say = { ok: result.ok, page_message: result.page_message ?? null, error: result.error ?? null };
      say('say', JSON.stringify(report.say));
      view = result.observe;
      continue;
    }
    const others = (view.shared_questions ?? []).filter((question) => question.can_vote && question.question !== QUESTION);
    if (!voted && others.length) {
      voted = true;
      const target = others[0];
      const result = await seat('vote', '--n', String(target.n));
      report.vote = { ok: result.ok, error: result.error ?? null, question: target.question };
      say('vote', JSON.stringify(report.vote));
      view = result.observe;
      continue;
    }
    if (!report.status) report.status = await seat('status');
    // While working the assignment, look up every few seconds to tick the next step.
    view = await seat('wait', '--for', 'change', '--timeout', view.assignment?.active ? '4' : '30');
  }
  for (const message of view.private_new ?? []) report.private.push(message);
  report.last_observe = await seat('observe', '--all');
}

async function main() {
  fs.mkdirSync(work, { recursive: true, mode: 0o700 });
  for (const name of ['seat.sock', 'session.json']) fs.rmSync(path.join(work, name), { force: true });
  const out = fs.openSync(path.join(work, 'classroom.out'), 'w');
  const child = spawn(process.execPath, [path.join(__dirname, 'classroom.cjs'), ...argv], { stdio: ['ignore', out, out] });
  const exited = new Promise((resolve) => child.on('exit', (code) => resolve(code)));
  const deadline = Date.now() + 120000;
  while (!fs.existsSync(path.join(work, 'seat.sock'))) {
    if (child.exitCode !== null) throw new Error(`classroom.cjs exited early; see ${path.join(work, 'classroom.out')}`);
    if (Date.now() > deadline) throw new Error('classroom.cjs did not get ready');
    await sleep(300);
  }
  say('session ready');
  const report = { answers: {}, feedback: {}, ticks: [], private: [], help: null, question: null, say: null, vote: null, status: null };
  try { await attend(report); } catch (error) { report.error = error.message; }
  say('lesson over; stopping');
  await seat('stop').catch(() => {});
  const code = await exited;
  const session = JSON.parse(fs.readFileSync(path.join(work, 'session.json'), 'utf8'));
  const finalView = report.last_observe;
  const activities = Object.keys(report.answers);
  report.checks = {
    every_activity_answered_and_acked: activities.length > 0 && activities.every((id) => report.answers[id].ok && report.answers[id].acked),
    personal_feedback_for_every_activity: activities.length > 0 && activities.every((id) => report.feedback[id]),
    assignment_all_ticked: Boolean(finalView?.assignment?.steps.every((step) => step.done)),
    hint_received: Boolean(report.help?.private?.some((message) => message.kind === 'ヒント')),
    question_answered: Boolean(report.question?.private?.some((message) => message.kind === '質問の答え')),
    lesson_ended: finalView?.lesson_state === 'ended',
    video_written: Boolean(session.video?.seconds > 0 && session.video.width > 0),
    classroom_exit_0: code === 0,
  };
  report.activities = activities;
  report.wall_clock_seconds = Math.round((Date.now() - began) / 1000);
  report.session = session;
  fs.writeFileSync(path.join(work, 'smoke-report.json'), JSON.stringify(report, null, 2));
  say('checks', JSON.stringify(report.checks));
  process.exit(Object.values(report.checks).every(Boolean) ? 0 : 1);
}

main().catch((error) => { console.error('[smoke] FAIL', error.message); process.exit(1); });
