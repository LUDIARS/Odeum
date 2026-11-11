// Condensed operator schedule derived from the lesson's own timeline.
//
// The service plays the timeline in real time (90 minutes for the sample). The schedule walks it
// section by section with the console's controls: let the section's narration show, open the
// section's question / assignment by hand, give the attendee a fixed window, close, reveal, and
// move on with 次のセクションへ before the timeline would reach that cue by itself. The last section
// runs to the lesson's own end (there is no console control that jumps to the end).
//
// A step list is checked against the timeline up front: if a manual step would land after the
// cue the timeline itself would fire in that section, the schedule refuses to start.

const DEFAULTS = {
  narrationSeconds: 8,     // narration and slide on screen before anything else in a section
  sectionSeconds: 15,      // a section with nothing to answer (e.g. the introduction)
  activitySeconds: 60,     // answer window per question
  revealSeconds: 12,       // results and personal feedback on screen
  assignmentSeconds: 120,  // assignment window
  shareAfterSeconds: 20,   // in a 良い質問の共有 section, narration before sharing
  actionSeconds: 2,        // allowance per console click (click + verification) when checking the fit
  marginSeconds: 10,       // kept free before the timeline's own cue
};
const AUTO_CUES = new Set(['activity.open', 'assignment.assign', 'assignment.close', 'questions.share']);

function sectionsOf(timeline) {
  const sections = [];
  for (const cue of timeline.cues) {
    if (cue.kind === 'section.start') sections.push({ id: cue.section, title: cue.detail.title, start: cue.at_ms, cues: [] });
    else if (sections.length && cue.section === sections[sections.length - 1].id) sections[sections.length - 1].cues.push(cue);
  }
  sections.forEach((section, index) => { section.end = index + 1 < sections.length ? sections[index + 1].start : timeline.duration_ms; });
  return sections;
}

// prompt text -> {id, kind, ...}: the student page shows the prompt, not the id.
function activitiesOf(timeline) {
  const out = new Map();
  for (const cue of timeline.cues) if (cue.kind === 'activity.open') out.set(cue.detail.prompt, { ...cue.detail, section: cue.section });
  return out;
}

function buildSchedule(timeline, settings = {}, windows = {}) {
  const s = { ...DEFAULTS, ...settings };
  const sections = sectionsOf(timeline);
  if (sections.length === 0) throw new Error('timeline has no sections');
  const steps = [];
  sections.forEach((section, index) => {
    const local = [];
    const action = (op, label, extra = {}) => local.push({ kind: 'action', op, label, section: section.id, ...extra });
    const hold = (seconds, label, purpose) => local.push({ kind: 'hold', seconds, label, purpose, section: section.id });
    const open = section.cues.find((cue) => cue.kind === 'activity.open');
    const assign = section.cues.find((cue) => cue.kind === 'assignment.assign');
    const share = section.cues.some((cue) => cue.kind === 'questions.share');
    const last = index === sections.length - 1;

    if (index === 0) action('start', '開始');
    else action('skip', `次のセクションへ → ${section.title}`, { expect: section.id });
    if (open) {
      const seconds = windows[open.detail.id] ?? s.activitySeconds;
      hold(s.narrationSeconds, `語り: ${section.title}`, 'narration');
      action('activity.open', `問いを出す: ${open.detail.id}`, { id: open.detail.id });
      hold(seconds, `回答受付: ${open.detail.id}`, 'answer');
      action('activity.close', `締め切る: ${open.detail.id}`, { id: open.detail.id });
      hold(2, '締め切り表示', 'pause');
      action('activity.reveal', `結果を見せる: ${open.detail.id}`, { id: open.detail.id });
      hold(s.revealSeconds, `結果: ${open.detail.id}`, 'reveal');
    }
    if (assign) {
      hold(s.narrationSeconds, `語り: ${section.title}`, 'narration');
      action('assignment.assign', `課題を出す: ${assign.detail.id}`, { id: assign.detail.id });
      hold(s.assignmentSeconds, `課題: ${assign.detail.id}`, 'work');
      action('assignment.close', `課題を終える: ${assign.detail.id}`, { id: assign.detail.id });
      hold(5, '課題の締め', 'pause');
    }
    if (share) {
      hold(s.shareAfterSeconds, `語り: ${section.title}`, 'narration');
      action('questions.share', '良い質問を共有する');
    }
    if (!open && !assign && !share) hold(s.sectionSeconds, `語り: ${section.title}`, 'narration');
    if (last) local.push({ kind: 'await-end', label: '授業の終わりを待つ', section: section.id, seconds: (section.end - section.start) / 1000 + 60 });

    // Every manual step must happen before the timeline's own cue in this section.
    const auto = section.cues.filter((cue) => AUTO_CUES.has(cue.kind)).map((cue) => (cue.at_ms - section.start) / 1000);
    const budget = auto.length ? Math.min(...auto) : (section.end - section.start) / 1000;
    let used = 0;
    for (const step of local) {
      if (step.kind === 'hold') used += step.seconds;
      if (step.kind === 'action') used += s.actionSeconds;
      if (step.kind === 'action' && step.op !== 'start' && step.op !== 'skip') step.before = budget;  // checked again at run time
    }
    if (!last && used + s.marginSeconds > budget)
      throw new Error(`schedule does not fit section "${section.id}": the condensed steps need ${used}s but the lesson's own cue comes at ${budget}s (+${s.marginSeconds}s margin); shorten the windows`);
    if (last) {
      const beforeShare = local.slice(0, local.findIndex((step) => step.op === 'questions.share') + 1)
        .reduce((sum, step) => sum + (step.kind === 'hold' ? step.seconds : step.kind === 'action' ? s.actionSeconds : 0), 0);
      if (share && beforeShare + s.marginSeconds > budget)
        throw new Error(`schedule does not fit section "${section.id}": sharing at ${beforeShare}s comes after the lesson's own share at ${budget}s`);
    }
    steps.push(...local);
  });
  return { settings: s, sections, steps };
}

// Rough wall-clock of the whole schedule, for the start-up banner and `status`.
function estimateSeconds(schedule) {
  return schedule.steps.reduce((sum, step) => {
    if (step.kind === 'hold') return sum + step.seconds;
    if (step.kind === 'action') return sum + schedule.settings.actionSeconds;
    const section = schedule.sections.find((item) => item.id === step.section);
    const consumed = schedule.steps.filter((item) => item.section === step.section && item.kind !== 'await-end')
      .reduce((total, item) => total + (item.kind === 'hold' ? item.seconds : schedule.settings.actionSeconds), 0);
    return sum + Math.max(0, (section.end - section.start) / 1000 - consumed);
  }, 0);
}

module.exports = { buildSchedule, estimateSeconds, activitiesOf, sectionsOf, DEFAULTS };
