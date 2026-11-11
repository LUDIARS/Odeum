// What a student seat shows, read from the real page DOM (the same thing the video shows).
// Both functions run inside the student page.

// Installed before the page's own scripts. Counts DOM mutations of the elements the page uses to
// report the outcome of an action (toast, answer state, private feed, ...), so an action can wait
// for "the page reacted" even when the new text equals the old one. Purely observational.
function installHooks() {
  const watched = ['feedback', 'activity-state', 'private', 'shared', 'conduct', 'enroll-code', 'assignment-status'];
  window.__attend = { mutations: Object.fromEntries(watched.map((id) => [id, 0])), seq: {} };
  const attach = () => {
    for (const id of watched) {
      const node = document.getElementById(id);
      if (!node) continue;
      new MutationObserver(() => { window.__attend.mutations[id] += 1; })
        .observe(node, { childList: true, characterData: true, subtree: true, attributes: true, attributeFilter: ['hidden', 'data-standing'] });
    }
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', attach); else attach();
}

// Returns a plain object describing the screen. Private messages and shared questions are
// numbered in arrival order (the page prepends them) with a data attribute that is not displayed.
function readSeat() {
  const required = ['entry', 'stage', 'lesson-title', 'section-title', 'status', 'conduct', 'narration', 'demo', 'slide', 'slide-title',
    'slide-points', 'activity', 'activity-prompt', 'activity-code', 'activity-choices', 'activity-text', 'activity-send', 'activity-state',
    'activity-results', 'activity-bars', 'activity-answer', 'activity-highlights', 'activity-personal', 'assignment', 'assignment-title',
    'assignment-brief', 'assignment-steps', 'help', 'assignment-status', 'private', 'say', 'question', 'show-name', 'shared', 'feedback'];
  const missing = required.filter((id) => !document.getElementById(id));
  if (missing.length) return { missing };
  const $ = (id) => document.getElementById(id);
  const text = (node) => (node?.textContent ?? '').replace(/\s+/g, ' ').trim();
  const shown = (node) => Boolean(node) && !node.hidden && !node.closest('[hidden]');
  const number = (list, key) => {
    // oldest first: the page prepends, so walk from the bottom
    const items = [...list.children].filter((item) => !item.classList.contains('ls-note')).reverse();
    for (const item of items) if (!item.dataset[key]) item.dataset[key] = String((window.__attend.seq[key] = (window.__attend.seq[key] ?? 0) + 1));
    return items;
  };

  const view = {};
  const status = text($('status'));
  const sectionTitle = text($('section-title'));
  if (!shown($('stage'))) view.lesson_state = 'not_joined';
  else if (status === '授業は閉じられました') view.lesson_state = 'closed';
  else if (sectionTitle === '授業は終わりました') view.lesson_state = 'ended';
  else if (/^\d+ \/ \d+ /.test(sectionTitle)) view.lesson_state = 'running';
  else view.lesson_state = 'waiting';
  view.connection = status;
  view.entry_status = shown($('entry')) ? text(document.getElementById('entry-status')) : '';
  view.lesson_title = text($('lesson-title'));
  const match = /^(\d+) \/ (\d+)\s+(.*)$/.exec(sectionTitle);
  view.section = match ? { index: Number(match[1]), total: Number(match[2]), title: match[3] } : { title: sectionTitle };
  view.slide = shown($('slide')) ? { title: text($('slide-title')), points: [...$('slide-points').children].map(text) } : null;
  view.narration = text($('narration'));
  view.demo = shown($('demo')) ? text($('demo')) : null;

  if (shown($('activity'))) {
    const choices = [...$('activity-choices').querySelectorAll('label')].map((row, index) => {
      const input = row.querySelector('input');
      return { index, label: text(row), selected: Boolean(input?.checked) };
    });
    const open = !$('activity-send').disabled;
    const revealed = shown($('activity-results'));
    view.activity = {
      prompt: text($('activity-prompt')),
      code: shown($('activity-code')) ? $('activity-code').textContent : null,
      input: choices.length ? ($('activity-choices').querySelector('input[type=checkbox]') ? 'choices (multi)' : 'choice (single)') : 'text',
      choices: choices.length ? choices : null,
      text_value: choices.length ? null : $('activity-text').value,
      state: revealed ? 'revealed' : open ? 'open' : 'closed',
      state_note: text($('activity-state')),
      results: revealed ? {
        bars: [...$('activity-bars').children].map((row) => ({ label: text(row.querySelector('.ls-bar-label')), count: Number(text(row.querySelector('.ls-bar-count'))), correct: row.dataset.correct === 'true' })),
        model_answer: shown($('activity-answer')) ? text($('activity-answer')) : null,
        highlights: [...$('activity-highlights').children].map(text),
      } : null,
      personal_feedback: shown($('activity-personal')) ? text($('activity-personal')) : null,
    };
  } else view.activity = null;

  if (shown($('assignment'))) {
    view.assignment = {
      title: text($('assignment-title')),
      brief: text($('assignment-brief')),
      steps: [...$('assignment-steps').children].map((item, index) => {
        const label = item.querySelector('label');
        const auto = Boolean(label?.querySelector('.ls-auto'));
        const words = [...(label?.childNodes ?? [])].filter((node) => node.nodeType === Node.TEXT_NODE).map((node) => node.textContent).join('').trim();
        return { n: index + 1, label: words, done: Boolean(item.querySelector('input')?.checked), auto };
      }),
      active: !$('help').disabled,
      status: text($('assignment-status')),
    };
  } else view.assignment = null;

  view.private = number($('private'), 'attendSeq').map((item) => ({
    seq: Number(item.dataset.attendSeq), kind: text(item.querySelector('.ls-kind')),
    text: [...item.childNodes].filter((node) => node.nodeType === Node.TEXT_NODE).map((node) => node.textContent).join('').trim(),
  }));
  view.shared = number($('shared'), 'attendShared').map((item) => {
    const button = item.querySelector('button');
    const meta = [...item.querySelectorAll('.ls-meta span')].map(text);
    return {
      n: Number(item.dataset.attendShared), question: text(item.querySelector('.ls-q')), answer: text(item.querySelector('.ls-a')),
      name: meta.length > 1 ? meta[0] : null, votes: text([...item.querySelectorAll('.ls-meta span')].find((span) => span.textContent.startsWith('知りたい'))),
      can_vote: Boolean(button) && !button.disabled,
    };
  });
  const conduct = $('conduct');
  view.conduct = { standing: conduct.dataset.standing || 'good', text: shown(conduct) ? text(conduct) : null };
  view.toast = text($('feedback'));
  view.mutations = { ...window.__attend.mutations };
  return view;
}

module.exports = { installHooks, readSeat };
