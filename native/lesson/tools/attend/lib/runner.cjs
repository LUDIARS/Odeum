// Plays the condensed schedule on the console and reports where it is (`seat.cjs status`).
const { perform, clock } = require('./operator.cjs');

function createRunner({ page, schedule, log, seat }) {
  const seatState = () => seat()?.lesson_state;
  let index = -1;
  let stepStarted = 0;
  let state = 'pending';   // pending -> running -> done | failed | stopped
  let failure = null;
  let wake = null;

  const sleep = (ms) => new Promise((resolve) => { const timer = setTimeout(resolve, ms); wake = () => { clearTimeout(timer); resolve(); }; });
  const describe = (step) => ({ kind: step.kind, op: step.op, label: step.label, section: step.section, seconds: step.seconds ?? null });

  async function awaitEnd(step) {
    const deadline = Date.now() + step.seconds * 1000;
    while (state === 'running') {
      if (seatState() === 'ended') return;
      if (Date.now() > deadline) throw new Error(`step "${step.label}": the lesson did not end within ${step.seconds}s`);
      await sleep(500);
    }
  }

  // A demo cue must reach the attendee's page (the line 先生の画面で実演中: …) before the schedule moves on.
  async function awaitDemo(step) {
    const deadline = Date.now() + step.seconds * 1000;
    while (state === 'running') {
      if ((seat()?.demo ?? '').includes(step.note)) { log.write('operator', { event: 'demo.seen', label: step.label, section: step.section }); return; }
      if (Date.now() > deadline) throw new Error(`step "${step.label}": the demo line did not reach the attendee's page within ${step.seconds}s`);
      await sleep(200);
    }
  }

  async function run() {
    state = 'running';
    log.write('operator', { event: 'schedule.start', steps: schedule.steps.length });
    for (index = 0; index < schedule.steps.length && state === 'running'; index += 1) {
      const step = schedule.steps[index];
      stepStarted = Date.now();
      if (step.kind === 'hold') { await sleep(step.seconds * 1000); continue; }
      if (step.kind === 'await-end') { await awaitEnd(step); log.write('operator', { event: 'lesson.ended' }); continue; }
      if (step.kind === 'await-demo') { await awaitDemo(step); continue; }
      if (step.deadline_ms !== undefined) {
        // The timeline closes by itself at its own time; closing by hand must come before that.
        const now = await clock(page);
        if (!now) throw new Error(`step "${step.label}": cannot read the console clock`);
        if (now.position * 1000 > step.deadline_ms)
          throw new Error(`step "${step.label}" is too late: the lesson is at ${now.position}s, the window was to end by ${Math.round(step.deadline_ms / 1000)}s`);
      }
      const result = await perform(page, step);
      log.write('operator', { event: step.op, label: step.label, section: step.section, id: step.id ?? null, console: result });
    }
    if (state === 'running') { state = 'done'; log.write('operator', { event: 'schedule.done' }); }
  }

  return {
    start() {
      return run().catch((error) => { state = 'failed'; failure = error; log.write('error', { where: 'schedule', message: error.message }); throw error; });
    },
    stop() { if (state === 'running' || state === 'pending') state = 'stopped'; wake?.(); },
    state: () => state,
    status() {
      const out = { state, failure: failure?.message ?? null };
      if (index < 0 || index >= schedule.steps.length) return { ...out, limits: schedule.limits, upcoming: index < 0 ? schedule.steps.slice(0, 5).map(describe) : [] };
      const step = schedule.steps[index];
      const elapsed = (Date.now() - stepStarted) / 1000;
      const left = step.kind === 'hold' ? Math.max(0, step.seconds - elapsed) : 0;
      let at = left;
      const upcoming = [];
      for (const next of schedule.steps.slice(index + 1)) {
        if (next.kind === 'action') upcoming.push({ ...describe(next), in_seconds: Math.round(at) });
        if (next.kind === 'await-end') { upcoming.push({ ...describe(next), in_seconds: Math.round(at) }); break; }
        if (next.kind === 'await-demo') continue;
        if (next.kind === 'hold') at += next.seconds;
        if (upcoming.length >= 5) break;
      }
      return { ...out, limits: schedule.limits, step: { n: index + 1, of: schedule.steps.length, ...describe(step), elapsed_seconds: Math.round(elapsed), left_seconds: Math.round(left) }, upcoming };
    },
  };
}

module.exports = { createRunner };
