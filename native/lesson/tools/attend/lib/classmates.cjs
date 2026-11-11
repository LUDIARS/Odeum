// Scripted classmates: fixed, plainly canned behaviour so the class tallies and the question
// board have more than one voice. They use the same page controls as the attendee.
//   - each question: answer once, after a per-seat delay, with a canned answer
//   - the assignment: tick one step every `stepSeconds`; the first classmate asks for a hint once
//   - one question each, in a fixed section; in the last section, 「私も知りたい」 on a shared question
const actions = require('./seat-actions.cjs');

// Canned answers for the sample course, by activity id. Other courses get the generic ones.
const CANNED = [
  {
    'missing-feel': '当たった瞬間に止まる感じと、相手が飛ばされる動きがない',
    'weapon-power': [1],
    'hitstop-design': '止める長さを決める。強い攻撃ほど長く止める',
    'knockback-effects': '相手との距離が離れて、連続で当てにくくなる',
    'enemy-placement': [1],
    'camera-open': 'プレイヤーの後ろからなめらかに追従する。揺れは控えめにして酔わないようにする',
    // Asked once the knockback question has been revealed: before that, its prepared answer is held back.
    question: { section: 'enemies', text: 'ノックバックの向きはどうやって決めればいいですか？' },
  },
  {
    'missing-feel': '音とエフェクト。あとダメージの数字',
    'weapon-power': [1, 2],
    'hitstop-design': '全体を止めるか、当たった二人だけ止めるかを決める。UI は止めない',
    'knockback-effects': '壁際だと押し付けられる。崖なら落ちる',
    'enemy-placement': [0],
    'camera-open': '少し引いて広く映し、敵が見えるようにする',
    question: { section: 'camera', text: 'カメラの揺れが強すぎると酔うのはなぜですか？' },
  },
];
const CLICK_MS = 10000;   // a classmate's click gives up sooner than Playwright's 30 s, so the screen is re-read while it still matters
const GENERIC = { text: 'まだ考え中ですが、見た目で分かることが大事だと思います', choice: [0] };

function createClassmate({ name, page, slot, observation, log, onError, stepSeconds = 20 }) {
  const script = CANNED[slot % CANNED.length];
  const delay = 10 + slot * 7;           // seconds after a question opens
  const answered = new Set();
  const openedAt = new Map();
  let lastStep = 0;
  let helped = false;
  let asked = false;
  let voted = false;
  let timer = null;
  let stopped = false;
  let busy = false;

  const record = (what, result) => log.write('classmate', { seat: name, what, ok: result.ok, error: result.error ?? null });

  // What the page offers right now. A classmate only attempts an action the screen offers, and every action
  // is optional: when it does not go through, the screen is read again. If the action is no longer offered
  // (the question closed, the assignment ended, the shared question was removed or already voted on, the
  // lesson ended) it is logged as skipped. If the page still offers it, the failure is a real one and stops
  // the session.
  const running = (view) => view.lesson_state === 'running';
  const OFFERS = {
    answer: (view, id) => running(view) && view.activity?.state === 'open' && observation.activityId(view.activity.prompt)?.id === id,
    tick: (view, n) => running(view) && Boolean(view.assignment?.active) && view.assignment.steps.some((step) => step.n === n && !step.done),
    help: (view) => running(view) && Boolean(view.assignment?.active),
    ask: (view) => running(view),
    vote: (view, question) => running(view) && view.shared.some((item) => item.question === question && item.can_vote),
  };
  const why = (view, kind) => (!running(view) ? `lesson ${view.lesson_state}`
    : { answer: 'the question is no longer open', tick: 'the step or the assignment is no longer open', help: 'the assignment is no longer open',
      ask: 'questions are not offered', vote: 'the shared question can no longer be voted on' }[kind]);
  async function attempt(what, kind, arg, run) {
    let result;
    let failure = null;
    try { result = await run(); } catch (error) { failure = error; }
    if (!failure && result.ok) { record(what, result); return; }
    const now = await actions.view(page);
    if (!OFFERS[kind](now, arg)) {
      log.write('classmate', { seat: name, what, ok: false, skipped: true, reason: why(now, kind), error: result?.error ?? (failure ? failure.message.split('\n')[0] : null) });
      return;
    }
    throw failure ?? new Error(`${what} was refused while the page still offers it: ${JSON.stringify(result.error)}`);
  }

  async function act(raw) {
    const activity = raw.activity && observation.activityId(raw.activity.prompt);
    if (activity && OFFERS.answer(raw, activity.id) && !answered.has(activity.id)) {
      if (!openedAt.has(activity.id)) openedAt.set(activity.id, Date.now());
      if (Date.now() - openedAt.get(activity.id) >= delay * 1000) {
        answered.add(activity.id);
        const canned = script[activity.id] ?? (activity.kind === 'choice' ? GENERIC.choice : GENERIC.text);
        await attempt(`answer ${activity.id}`, 'answer', activity.id,
          () => (Array.isArray(canned) ? actions.answer(page, { choices: canned }) : actions.answer(page, { text: canned })));
      }
    }
    if (OFFERS.help(raw)) {
      const next = raw.assignment.steps.find((step) => !step.done);
      if (next && Date.now() - lastStep >= stepSeconds * 1000) {
        if (lastStep === 0) lastStep = Date.now();    // start working a little after it is given
        else { lastStep = Date.now(); await attempt(`tick ${next.n}`, 'tick', next.n, () => actions.tick(page, { step: next.n })); }
      }
      if (slot === 0 && !helped && raw.assignment.steps.filter((step) => step.done).length >= 2) {
        helped = true;
        await attempt('help', 'help', null, () => actions.help(page));
      }
    }
    if (!asked && script.question && raw.sectionTitleId === script.question.section && OFFERS.ask(raw)) {
      asked = true;
      await attempt('ask', 'ask', null, () => actions.ask(page, { text: script.question.text, showName: slot === 0 }));
    }
    if (!voted && running(raw) && raw.section?.index && raw.section.index === raw.section.total && raw.shared.length) {
      const target = raw.shared.find((question) => question.can_vote && question.question !== script.question?.text);
      if (target) {
        voted = true;
        await attempt(`vote ${target.n}`, 'vote', target.question, () => actions.vote(page, { n: target.n, timeout: CLICK_MS }));
      }
    }
  }

  async function loop(sectionIdOf) {
    if (stopped) return;
    if (!busy) {
      busy = true;
      try {
        const raw = await actions.view(page);
        raw.sectionTitleId = sectionIdOf(raw.section?.title);
        await act(raw);
      } catch (error) {
        if (!stopped) { onError(new Error(`classmate ${name} failed: ${error.message}`)); return; }
      } finally { busy = false; }
    }
    if (!stopped) timer = setTimeout(() => loop(sectionIdOf), 1000);
  }

  return {
    name,
    start(sectionIdOf) { loop(sectionIdOf); },
    stop() { stopped = true; clearTimeout(timer); },
  };
}

module.exports = { createClassmate };
