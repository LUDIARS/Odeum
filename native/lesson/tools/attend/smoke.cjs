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
// No prepared answer; the scripted teacher (teacher-answers.action-game.json) has one.
const TEACHER_QUESTION = 'ノックバックで次の攻撃が届かなくなる問題は、実際のゲームではどう解決していますか';
// A prepared answer exists; the attendee says まだ分からない, and the scripted teacher answers it.
const UNRESOLVED_QUESTION = 'ダメージと威力を分けておくと、具体的にどんなときに便利なんですか？';
// Neither the course nor the scripted teacher answers this one: it is still waiting at the end.
const LEFT_QUESTION = 'ノックバックの強さは敵ごとに変えるべきですか';
const TROUBLE = '先生、すみません、デモの画面が小さくて world= の数字がどう縮んだのかよく見えませんでした。';
const TEXT = 'ヒットストップで一瞬止める、ノックバックで押し返す、カメラを揺らす。威力で止める長さを変え、UI は止めない。壁際や連続ヒットの扱いも決める。';

async function attend(report) {
  const answered = new Set();
  let view = await seat('observe');
  let asked = false, spoke = false, voted = false, helped = false, linked = false, remarked = false, troubled = false, lastTick = 0;
  let teacherAsked = false, unresolvedAsked = false, unresolvedMarked = false, leftAsked = false;
  // Every private message must come back exactly once from observe / wait (not from action replies).
  const collect = (observed) => { for (const message of observed.private_new ?? []) report.private.push(message); };
  while (view.lesson_state !== 'ended' && view.lesson_state !== 'closed') {
    collect(view);
    if (view.demo) report.demos[view.section?.index ?? 0] = view.demo;
    report.mine = view.my_questions ?? report.mine;
    const activity = view.activity;
    if (activity?.state === 'open' && activity.id && !answered.has(activity.id)) {
      answered.add(activity.id);
      const multi = activity.input.includes('multi');
      const result = activity.choices ? await seat('answer', '--choices', multi ? '1,2' : '1') : await seat('answer', '--text', TEXT);
      report.answers[activity.id] = { ok: result.ok, acked: result.acked ?? false, error: result.error ?? null };
      say('answer', activity.id, result.ok ? 'acked' : JSON.stringify(result.error));
      view = await seat('observe');
      continue;
    }
    if (activity?.id && activity.personal_feedback) report.feedback[activity.id] = activity.personal_feedback;
    if (activity?.id && activity.results?.explanation) report.explanations[activity.id] = activity.results.explanation;
    if (view.assignment) report.last_assignment = view.assignment;
    // Said to the teacher after answering a text question: must not replace the answer.
    if (!remarked && activity?.state === 'open' && activity.id === 'hitstop-design' && answered.has(activity.id)) {
      remarked = true;
      const result = await seat('say', '--text', 'デモが文字だけだと分かりにくかったです');
      report.remark = { ok: result.ok, taken_as: result.taken_as ?? null, taken_as_answer: result.taken_as_answer ?? false, page_message: result.page_message ?? null,
        answer_on_page: result.observe.activity?.answer_on_page ?? null };
      say('remark', JSON.stringify(report.remark));
      view = await seat('observe');
      continue;
    }
    // The attended sentence: could not see the demo. Must be acknowledged as such, not filed as a question.
    if (remarked && !troubled && activity?.state === 'open' && activity.id === 'hitstop-design') {
      troubled = true;
      const result = await seat('say', '--text', TROUBLE);
      report.trouble = { ok: result.ok, taken_as: result.taken_as ?? null, page_message: result.page_message ?? null,
        mine_after: result.observe.my_questions.length, answer_on_page: result.observe.activity?.answer_on_page ?? null };
      say('trouble', JSON.stringify(report.trouble));
      view = await seat('observe');
      continue;
    }
    if (!linked && view.tool) {
      linked = true;
      const result = await seat('tool-link');
      report.tool_link = { ok: result.ok, shape_ok: /\/v1\/tool#[0-9A-Z]{10}$/.test(result.link ?? ''), error: result.error ?? null };
      say('tool-link', JSON.stringify(report.tool_link));
      view = await seat('observe');
      continue;
    }
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
          say('help', hint.ok ? JSON.stringify(hint.observe.private_new.map((m) => m.text)) : JSON.stringify(hint.error));
        }
        view = (await seat('observe'));
        continue;
      }
    }
    // Asked after knockback-effects is revealed: its prepared answer (damage vs power) gives that question away until then.
    if (!unresolvedAsked && view.section?.index >= 6) {
      unresolvedAsked = true;
      const result = await seat('ask', '--text', UNRESOLVED_QUESTION);
      say('ask (prepared answer)', result.ok ? 'ok' : JSON.stringify(result.error));
      view = await seat('observe');
      continue;
    }
    const prepared = (view.my_questions ?? []).find((item) => item.question === UNRESOLVED_QUESTION);
    if (!unresolvedMarked && prepared?.can_unresolve) {
      unresolvedMarked = true;
      const result = await seat('unresolved', '--n', String(prepared.n));
      const after = result.observe.my_questions.find((item) => item.n === prepared.n);
      report.unresolved = { ok: result.ok, error: result.error ?? null, before: prepared.state, after: after?.state ?? null };
      say('unresolved', JSON.stringify(report.unresolved));
      view = await seat('observe');
      continue;
    }
    // After hitstop-design is revealed, so the prepared answer on hit stop length may be given.
    if (!asked && view.section?.index >= 5) {
      asked = true;
      const result = await seat('ask', '--text', QUESTION);
      report.question = { ok: result.ok, private: result.observe.private_new, error: result.error ?? null };
      say('ask', result.ok ? JSON.stringify(result.observe.private_new.map((m) => m.text)) : JSON.stringify(result.error));
      view = await seat('observe');
      continue;
    }
    if (!teacherAsked && view.section?.index >= 5) {
      teacherAsked = true;
      const result = await seat('ask', '--text', TEACHER_QUESTION);
      say('ask (for the teacher)', result.ok ? 'ok' : JSON.stringify(result.error));
      view = await seat('observe');
      continue;
    }
    if (!leftAsked && view.section?.index >= 6) {
      leftAsked = true;
      const result = await seat('ask', '--text', LEFT_QUESTION);
      say('ask (left waiting)', result.ok ? 'ok' : JSON.stringify(result.error));
      view = await seat('observe');
      continue;
    }
    if (!spoke && view.section?.index >= 5) {
      spoke = true;
      const result = await seat('say', '--text', 'ノックバックの向きが気になりました');
      report.say = { ok: result.ok, page_message: result.page_message ?? null, error: result.error ?? null };
      say('say', JSON.stringify(report.say));
      view = await seat('observe');
      continue;
    }
    const others = (view.shared_questions ?? []).filter((question) => question.can_vote && question.question !== QUESTION);
    if (!voted && others.length) {
      voted = true;
      const target = others[0];
      const result = await seat('vote', '--n', String(target.n));
      report.vote = { ok: result.ok, error: result.error ?? null, question: target.question };
      say('vote', JSON.stringify(report.vote));
      view = await seat('observe');
      continue;
    }
    if (!report.status) report.status = await seat('status');
    // While working the assignment, look up every few seconds to tick the next step.
    view = await seat('wait', '--for', 'change', '--timeout', view.assignment?.active ? '4' : '30');
  }
  collect(view);
  report.last_observe = await seat('observe', '--all');
  collect(report.last_observe);
  report.mine = report.last_observe.my_questions;
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
  const report = { answers: {}, feedback: {}, explanations: {}, ticks: [], private: [], demos: {}, mine: [], help: null, question: null, say: null, vote: null,
    remark: null, trouble: null, unresolved: null, tool_link: null, last_assignment: null, status: null };
  try { await attend(report); } catch (error) { report.error = error.message; }
  say('lesson over; stopping');
  await seat('stop').catch(() => {});
  const code = await exited;
  const session = JSON.parse(fs.readFileSync(path.join(work, 'session.json'), 'utf8'));
  const finalView = report.last_observe;
  const activities = Object.keys(report.answers);
  const all = (finalView?.private_all ?? []).map((message) => message.seq);
  const returned = report.private.map((message) => message.seq);
  const state = (text) => report.mine.find((item) => item.question === text)?.state ?? null;
  const events = fs.readFileSync(path.join(work, 'attendee.jsonl'), 'utf8').trim().split('\n').map((line) => JSON.parse(line));
  report.teacher_answers = events.filter((event) => event.kind === 'operator' && event.event === 'teacher.answer').map((event) => event.entry);
  report.checks = {
    every_activity_answered_and_acked: activities.length > 0 && activities.every((id) => report.answers[id].ok && report.answers[id].acked),
    personal_feedback_for_every_activity: activities.length > 0 && activities.every((id) => report.feedback[id]),
    assignment_all_ticked: Boolean(report.last_assignment?.steps.every((step) => step.done)),
    automatic_steps_say_what_they_watch: Boolean(report.last_assignment?.steps.filter((step) => step.auto).every((step) => step.watches?.includes('feel.'))),
    choice_explanations_shown: Boolean(report.explanations['weapon-power'] && report.explanations['enemy-placement']),
    remark_kept_answer: Boolean(report.remark?.ok && !report.remark.taken_as_answer && report.remark.answer_on_page === TEXT),
    tool_link_returned: Boolean(report.tool_link?.ok && report.tool_link.shape_ok),
    hint_received: Boolean(report.help?.private?.some((message) => message.kind === 'ヒント')),
    question_answered: Boolean(report.private.some((message) => message.kind === '質問の答え')),
    intro_demo_seen: Boolean(report.demos[1]) && events.some((event) => event.kind === 'operator' && event.event === 'demo.seen' && event.section === 'intro'),
    trouble_acknowledged_not_a_question: Boolean(report.trouble?.ok && report.trouble.taken_as === 'trouble' && report.trouble.answer_on_page === TEXT
      && !report.mine.some((item) => item.question === TROUBLE)),
    remark_acknowledged: report.remark?.taken_as === 'remark',
    operator_answer_reached_attendee: report.private.some((message) => message.kind === '先生の答え') && state(TEACHER_QUESTION) === '先生から回答',
    unresolved_back_to_waiting_then_answered: report.unresolved?.after === '先生の回答待ち' && state(UNRESOLVED_QUESTION) === '先生から回答',
    unanswered_told_at_end: state(LEFT_QUESTION) === '先生の回答待ち' && report.private.some((message) => message.kind === '答えられなかった質問'),
    every_private_message_returned_once: all.length > 0 && returned.length === all.length && new Set(returned).size === all.length && all.every((seq) => returned.includes(seq)),
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
