// Condensed operator schedule derived from the lesson's own timeline.
//
// The service plays the timeline in real time (90 minutes for the sample). The schedule walks it
// section by section with the console's controls: let the section's first narration show, then bring
// each of the section's own cues forward with 「この合図まで進める」 (the console seeks to the cue and the
// timeline runs it itself: the demo line, the question, the assignment), give the attendee a window,
// and close early with the console's buttons. 次のセクションへ moves on.
//
// Because the timeline opens each question and assignment itself, the only limit on a window is the
// lesson's own: the time from the cue to the timeline's close (the question's `seconds`, the
// assignment's minutes). limitsOf() computes them. A window asked for (--windows, --choice-seconds, ...) that
// is longer than its limit is refused up front; a built-in default that is longer is clamped to the limit,
// and the schedule records it (`clamped`, shown by seat.cjs status).

const DEFAULTS = {
  narrationSeconds: 8,     // narration and slide on screen before anything else in a section
  sectionSeconds: 15,      // a section with nothing to answer (e.g. the introduction), after its demos
  demoSeconds: 8,          // a demo line on screen before moving on
  choiceSeconds: 60,       // answer window for a choice question
  textSeconds: 90,         // answer window for a text / missing / open question (2-4 sentences take longer to write)
  revealSeconds: 12,       // results and personal feedback on screen
  assignmentSeconds: 240,  // assignment window
  shareAfterSeconds: 20,   // in a 良い質問の共有 section, narration before sharing
  shareHoldSeconds: 20,    // shared questions on screen (votes) before the end
  actionSeconds: 2,        // allowance per console click (click + verification) for the estimate
  marginSeconds: 5,        // kept free before the timeline's own close
};
const DEMO_WAIT_SECONDS = 10;   // how long a demo cue may take to reach the attendee's page

function sectionsOf(timeline) {
  const sections = [];
  timeline.cues.forEach((cue, index) => {
    const indexed = { ...cue, index };
    if (cue.kind === 'section.start') sections.push({ id: cue.section, title: cue.detail.title, start: cue.at_ms, cues: [] });
    else if (sections.length && cue.section === sections[sections.length - 1].id) sections[sections.length - 1].cues.push(indexed);
  });
  sections.forEach((section, index) => { section.end = index + 1 < sections.length ? sections[index + 1].start : timeline.duration_ms; });
  return sections;
}

// prompt text -> {id, kind, ...}: the student page shows the prompt, not the id.
function activitiesOf(timeline) {
  const out = new Map();
  for (const cue of timeline.cues) if (cue.kind === 'activity.open') out.set(cue.detail.prompt, { ...cue.detail, section: cue.section });
  return out;
}

// The longest window the lesson's own timeline allows: from the open / assign cue to its close cue.
function limitsOf(timeline, marginSeconds = DEFAULTS.marginSeconds) {
  const limits = { activities: {}, assignments: {} };
  for (const cue of timeline.cues) {
    if (cue.kind === 'activity.open') {
      const close = timeline.cues.find((other) => other.kind === 'activity.close' && other.detail.id === cue.detail.id);
      limits.activities[cue.detail.id] = Math.floor((close.at_ms - cue.at_ms) / 1000) - marginSeconds;
    }
    if (cue.kind === 'assignment.assign') {
      const close = timeline.cues.find((other) => other.kind === 'assignment.close' && other.detail.id === cue.detail.id);
      limits.assignments[cue.detail.id] = Math.floor((close.at_ms - cue.at_ms) / 1000) - marginSeconds;
    }
  }
  return limits;
}
function describeLimits(limits) {
  const rows = [...Object.entries(limits.activities).map(([id, seconds]) => `  question ${id}: up to ${seconds}s`),
    ...Object.entries(limits.assignments).map(([id, seconds]) => `  assignment ${id}: up to ${seconds}s`)];
  return rows.join('\n');
}
// The windows a schedule uses, for --help: per question, with the defaults that had to be clamped.
function describeWindows(schedule) {
  const rows = Object.entries(schedule.windows).map(([id, seconds]) => `  question ${id}: ${seconds}s`);
  for (const cut of schedule.clamped) rows.push(`  (default ${cut.default_seconds}s for ${cut.id} clamped to ${cut.used_seconds}s: ${cut.reason})`);
  rows.push(`  assignment: ${schedule.settings.assignmentSeconds}s`);
  return rows.join('\n');
}

const textKinds = new Set(['text', 'missing', 'open']);

// settings: values the user gave (undefined = the default). windows: per-activity seconds the user gave.
function buildSchedule(timeline, settings = {}, windows = {}) {
  const given = Object.fromEntries(Object.entries(settings).filter(([, value]) => value !== undefined));
  const s = { ...DEFAULTS, ...given };
  const chosen = {};      // activity id -> window used
  const clamped = [];     // defaults cut to the lesson's limit
  const sections = sectionsOf(timeline);
  if (sections.length === 0) throw new Error('timeline has no sections');
  const limits = limitsOf(timeline, s.marginSeconds);
  const end = timeline.cues.findIndex((cue) => cue.kind === 'lesson.end');
  const steps = [];
  sections.forEach((section, index) => {
    const local = [];
    const action = (op, label, extra = {}) => local.push({ kind: 'action', op, label, section: section.id, ...extra });
    const hold = (seconds, label, purpose) => local.push({ kind: 'hold', seconds, label, purpose, section: section.id });
    const cueAction = (cue, label, expect) => action('cue', label, { cue: cue.index, at_ms: cue.at_ms, expect });
    const awaitDemo = (cue) => local.push({ kind: 'await-demo', label: `実演の表示を待つ: ${cue.detail.note || cue.detail.feature}`, section: section.id,
      note: cue.detail.note || cue.detail.feature, seconds: DEMO_WAIT_SECONDS });
    const last = index === sections.length - 1;

    if (index === 0) action('start', '開始');
    else action('skip', `次のセクションへ → ${section.title}`, { expect: section.id });
    // Demos at the section's very start arrive with it; they are confirmed on the attendee's page.
    for (const cue of section.cues) if (cue.kind === 'demo' && cue.at_ms === section.start) awaitDemo(cue);
    hold(s.narrationSeconds, `語り: ${section.title}`, 'narration');

    let acted = false;
    for (const cue of section.cues) {
      if (cue.kind === 'demo' && cue.at_ms > section.start) {
        cueAction(cue, `実演の合図まで進める: ${cue.detail.note || cue.detail.feature}`, 'demo');
        awaitDemo(cue);
        hold(s.demoSeconds, `実演: ${cue.detail.note || cue.detail.feature}`, 'demo');
      } else if (cue.kind === 'activity.open') {
        const id = cue.detail.id;
        const key = textKinds.has(cue.detail.kind) ? 'textSeconds' : 'choiceSeconds';
        const asked = windows[id] !== undefined || given[key] !== undefined;
        let seconds = windows[id] ?? s[key];
        if (seconds > limits.activities[id]) {
          if (asked) throw new Error(`question "${id}": a ${seconds}s window is longer than the lesson allows (${limits.activities[id]}s: its own ${cue.detail.seconds}s minus a ${s.marginSeconds}s margin)`);
          clamped.push({ id, default_seconds: seconds, used_seconds: limits.activities[id], reason: `the lesson closes "${id}" by itself after ${cue.detail.seconds}s` });
          seconds = limits.activities[id];
        }
        chosen[id] = seconds;
        cueAction(cue, `問いの合図まで進める: ${id}`, 'activity');
        hold(seconds, `回答受付: ${id}`, 'answer');
        action('activity.close', `締め切る: ${id}`, { id, deadline_ms: cue.at_ms + (seconds + s.marginSeconds) * 1000 });
        hold(2, '締め切り表示', 'pause');
        action('activity.reveal', `結果を見せる: ${id}`, { id });
        hold(s.revealSeconds, `結果: ${id}`, 'reveal');
        acted = true;
      } else if (cue.kind === 'assignment.assign') {
        const id = cue.detail.id;
        if (s.assignmentSeconds > limits.assignments[id])
          throw new Error(`assignment "${id}": a ${s.assignmentSeconds}s window is longer than the lesson allows (${limits.assignments[id]}s)`);
        cueAction(cue, `課題の合図まで進める: ${id}`, 'assignment');
        hold(s.assignmentSeconds, `課題: ${id}`, 'work');
        action('assignment.close', `課題を終える: ${id}`, { id, deadline_ms: cue.at_ms + (s.assignmentSeconds + s.marginSeconds) * 1000 });
        hold(5, '課題の締め', 'pause');
        acted = true;
      } else if (cue.kind === 'questions.share') {
        hold(s.shareAfterSeconds, `語り: ${section.title}`, 'narration');
        action('questions.share', '良い質問を共有する');
        hold(s.shareHoldSeconds, '共有された質問', 'share');
        acted = true;
      }
    }
    if (!acted && !section.cues.some((cue) => cue.kind === 'demo' && cue.at_ms > section.start)) hold(s.sectionSeconds, `語り: ${section.title}`, 'narration');
    if (last) {
      if (end >= 0) action('cue', '授業の終わりまで進める', { cue: end, at_ms: timeline.cues[end].at_ms, expect: 'end' });
      local.push({ kind: 'await-end', label: '授業の終わりを待つ', section: section.id, seconds: 60 });
    }
    steps.push(...local);
  });
  return { settings: s, sections, steps, limits, windows: chosen, clamped };
}

// Rough wall-clock of the whole schedule, for the start-up banner and `status`.
function estimateSeconds(schedule) {
  return schedule.steps.reduce((sum, step) => sum + (step.kind === 'hold' ? step.seconds : step.kind === 'action' ? schedule.settings.actionSeconds : 1), 0);
}

module.exports = { buildSchedule, estimateSeconds, activitiesOf, sectionsOf, limitsOf, describeLimits, describeWindows, DEFAULTS };
