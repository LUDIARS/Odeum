// Plays the condensed schedule on the console and reports where it is (`seat.cjs status`).
const { perform, clock } = require('./operator.cjs');

function createRunner({ page, schedule, log, seatState }) {
  let index = -1;
  let stepStarted = 0;
  let state = 'pending';   // pending -> running -> done | failed | stopped
  let failure = null;
  let wake = null;
  let sectionStart = 0;    // the section's start on the lesson clock, in seconds

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

  async function run() {
    state = 'running';
    log.write('operator', { event: 'schedule.start', steps: schedule.steps.length });
    for (index = 0; index < schedule.steps.length && state === 'running'; index += 1) {
      const step = schedule.steps[index];
      stepStarted = Date.now();
      if (step.kind === 'hold') { await sleep(step.seconds * 1000); continue; }
      if (step.kind === 'await-end') { await awaitEnd(step); log.write('operator', { event: 'lesson.ended' }); continue; }
      if (step.before !== undefined) {
        const now = await clock(page);
        if (!now) throw new Error(`step "${step.label}": cannot read the console clock`);
        if (now.position - sectionStart > step.before - 2)
          throw new Error(`step "${step.label}" is too late: ${now.position - sectionStart}s into the section, the lesson's own cue is at ${step.before}s`);
      }
      const result = await perform(page, step);
      if (step.op === 'start' || step.op === 'skip') sectionStart = schedule.sections.find((section) => section.id === step.section).start / 1000;
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
      if (index < 0 || index >= schedule.steps.length) return { ...out, upcoming: index < 0 ? schedule.steps.slice(0, 5).map(describe) : [] };
      const step = schedule.steps[index];
      const elapsed = (Date.now() - stepStarted) / 1000;
      const left = step.kind === 'hold' ? Math.max(0, step.seconds - elapsed) : 0;
      let at = left;
      const upcoming = [];
      for (const next of schedule.steps.slice(index + 1)) {
        if (next.kind === 'action') upcoming.push({ ...describe(next), in_seconds: Math.round(at) });
        if (next.kind === 'await-end') { upcoming.push({ ...describe(next), in_seconds: Math.round(at) }); break; }
        if (next.kind === 'hold') at += next.seconds;
        if (upcoming.length >= 5) break;
      }
      return { ...out, step: { n: index + 1, of: schedule.steps.length, ...describe(step), elapsed_seconds: Math.round(elapsed), left_seconds: Math.round(left) }, upcoming };
    },
  };
}

module.exports = { createRunner };
