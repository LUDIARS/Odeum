// Operator console: http://127.0.0.1:<console port>/console#token=<token from the token file>
// The token is taken from the fragment (never sent in an HTTP request for the page) and the
// fragment is cleared, so it does not stay in the address bar or the history.
import { connectWithBackoff, relaySocketUrl } from './socket.js';

const $ = (id) => document.getElementById(id);
const el = (tag, className, text) => {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
};
const LOG_KEEP = 80;
const STANDING = { good: ['', ''], warned: ['注意', 'warn'], locked: ['発言停止', 'danger'], ejected: ['退出', 'danger'] };
const PROGRESS = { not_started: '未着手', working: '作業中', stuck: '止まっている', needs_teacher: '直接見る', done: '完了' };
const INTENT = { question: '質問', answer: '回答', remark: '回答済みの人の発言', help: '助けて', chatter: '発言', disruption: 'ふざけ', unattributed: '話者不明' };
const REASON = { question_needs_teacher: '答えを用意できない質問です (「生徒の質問」で答えるか見送ってください)', asked_for_help: '助けを求めています', hints_exhausted: 'ヒントを出し切りました' };
const ERRORS = {
  console_token_required: 'トークンが違います。トークンファイルの値を付けて開き直してください。',
  invalid_state: 'いまはその操作ができません。', unknown_class: '授業を選んでください。', capacity: '上限に達しています。',
};

const token = new URLSearchParams(location.hash.slice(1)).get('token') ?? '';
history.replaceState(null, '', location.pathname);

let names = new Map();
let welcome = null;
let selected = '';
let attached = false;   // this socket has been attached to `selected`

const connection = connectWithBackoff({
  url: () => relaySocketUrl('/v1/console', { token }),
  onMessage: (message) => handlers[message.type]?.(message),
  onState: (state) => {
    $('status').textContent = { connecting: '接続中…', waiting: '再接続を待っています…' }[state] ?? $('status').textContent;
    if (state === 'connecting') attached = false;
  },
});
function send(message) { if (!connection.send(message)) $('status').textContent = '接続していません'; }
function log(kind, text, tone) {
  const item = el('li');
  const tag = el('span', 'co-tag', kind);
  if (tone) tag.dataset.tone = tone;
  item.append(tag, document.createTextNode(` ${text}`));
  $('log').prepend(item);
  while ($('log').children.length > LOG_KEEP) $('log').lastElementChild.remove();
}
const nameOf = (id) => (id ? names.get(id) ?? id : '不明');
const clock = (ms) => `${Math.floor(ms / 60000)}:${String(Math.floor(ms / 1000) % 60).padStart(2, '0')}`;
function options(select, items, label) {
  const keep = select.value;
  select.replaceChildren(...items.map((item) => { const option = el('option', '', label(item)); option.value = item.id; return option; }));
  if (items.some((item) => item.id === keep)) select.value = keep;
}
function button(text, className, onClick) {
  const node = el('button', className, text);
  node.type = 'button';
  node.addEventListener('click', onClick);
  return node;
}

function renderCatalog(message) {
  // The catalog is the first message on every connection. A reconnect is a new console session,
  // so attach to the class that was on screen.
  $('status').textContent = '接続中';
  if (selected && !attached && message.classes.some((item) => item.id === selected)) { attached = true; send({ type: 'class.select', class: selected }); }
  $('model').textContent = `判断モデル: ${message.model}`;
  options($('plots'), message.plots, (plot) => `${plot.title} (${plot.minutes} 分)`);
  options($('classes'), [{ id: '', title: '選択してください', code: '' }, ...message.classes], (item) => (item.id ? `${item.title} / ${item.code}` : item.title));
  $('classes').value = selected;
  const current = message.classes.find((item) => item.id === selected);
  $('class-bar').hidden = !current;
  $('workspace').hidden = !current;
  if (current) $('code').textContent = `${current.code.slice(0, 5)}-${current.code.slice(5)}`;
  $('prep').href = `/v1/prep.pptx?plot=${encodeURIComponent($('plots').value)}`;
}
function renderWelcome(message) {
  welcome = message;
  selected = message.class;
  attached = true;
  options($('activities'), message.activities, (item) => item.prompt.slice(0, 28));
  options($('assignments'), message.assignments, (item) => item.title);
  $('sections').replaceChildren(...message.sections.map((section) => {
    const start = message.timeline.cues.find((cue) => cue.kind === 'section.start' && cue.section === section.id);
    const node = button(section.title, '', () => send({ type: 'lesson.seek', position_ms: start.at_ms }));
    node.dataset.section = section.id;
    return node;
  }));
  send({ type: 'catalog' });
  renderSnapshot(message.snapshot);
}
function renderRoster(snapshot) {
  names = new Map(snapshot.students.map((student) => [student.id, student.name]));
  $('roster-empty').hidden = snapshot.students.length > 0;
  $('roster').replaceChildren(...snapshot.students.map((student) => {
    const row = el('tr');
    row.dataset.connected = String(student.connected);
    const who = el('td');
    const dot = el('span', 'co-dot');
    dot.dataset.on = String(student.connected);
    who.append(dot, document.createTextNode(student.name));

    const mood = el('td');
    if (student.speaking) mood.append(el('span', 'co-speaking', '話し中 '));
    if (student.tool) mood.append(el('span', 'co-tag', '道具 '));
    const [label, tone] = STANDING[student.standing];
    if (label) {
      const tag = el('span', 'co-tag', student.standing === 'locked' ? `${label} あと${Math.ceil(student.remaining_ms / 1000)}秒` : label);
      tag.dataset.tone = tone;
      mood.append(tag);
    }

    const work = el('td');
    if (student.progress) {
      const dots = el('span', 'co-stepdots');
      for (const done of student.progress.done) { const step = el('i'); step.dataset.done = String(done); dots.append(step); }
      const state = el('span', 'co-tag', PROGRESS[student.progress.state]);
      if (student.progress.state === 'stuck' || student.progress.state === 'needs_teacher') state.dataset.tone = 'warn';
      if (student.progress.state === 'done') state.dataset.tone = 'good';
      work.append(dots, document.createTextNode(' '), state);
    }
    const answered = el('td', '', snapshot.activity ? (student.answered ? '済' : '—') : '');

    const actions = el('td');
    const group = el('div', 'co-actions');
    if (student.standing === 'ejected') group.append(button('戻す', 'ls-secondary', () => send({ type: 'student.readmit', student: student.id })));
    else {
      group.append(student.standing === 'locked'
        ? button('解除', 'ls-secondary', () => send({ type: 'student.unlock', student: student.id }))
        : button('1分止める', 'ls-secondary', () => send({ type: 'student.lock', student: student.id, seconds: 60 })));
      group.append(button('退出', 'ls-danger-button', () => { if (confirm(`${student.name} さんを退出させます。入室も締め切られます。`)) send({ type: 'student.eject', student: student.id }); }));
      group.append(button('声をかける', 'ls-secondary', () => { const text = prompt(`${student.name} さんへ`); if (text?.trim()) send({ type: 'student.tell', student: student.id, text: text.trim() }); }));
    }
    actions.append(group);
    row.append(who, mood, work, answered, actions);
    return row;
  }));
}
function bar(label, count, total, correct) {
  const row = el('div', 'ls-bar');
  if (correct) row.dataset.correct = 'true';
  const track = el('div', 'ls-bar-track');
  const fill = el('div', 'ls-bar-fill');
  fill.style.width = `${total > 0 ? Math.round((count / total) * 100) : 0}%`;
  track.append(fill);
  row.append(el('span', 'ls-bar-label', label), el('span', 'ls-bar-count', String(count)), track);
  return row;
}
function renderActivity(activity, connected) {
  $('activity-prompt').textContent = activity ? activity.prompt : 'いまは出していません。';
  $('activity-bars').replaceChildren();
  $('activity-highlights').replaceChildren();
  $('activity-count').textContent = '';
  if (!activity) return;
  const tally = activity.tally;
  const state = { open: '受付中', closed: '締め切り', revealed: '結果を表示中' }[activity.state];
  $('activity-count').textContent = `${state} — ${tally.answered} / ${connected} 人が回答`;
  if (activity.kind === 'choice') activity.choices.forEach((label, index) => $('activity-bars').append(bar(label, tally.counts[index], tally.answered, (tally.correct ?? []).includes(index))));
  else for (const point of tally.points) $('activity-bars').append(bar(point.label, point.count, tally.answered, false));
  $('activity-highlights').replaceChildren(...(tally.highlights ?? []).map((text) => el('li', '', text)));
}
// Questions the model could not answer stay at the top, marked, until the operator answers or
// dismisses them. The list is rebuilt only when it changes, and drafts survive a rebuild, so the
// snapshot every 500 ms does not wipe what the operator is typing.
let questionsShown = '';
const answerDrafts = new Map();   // question id -> its answer field
function answerField(question) {
  let form = answerDrafts.get(question.id);
  if (form) return form;
  form = el('form', 'ls-row');
  const input = el('input');
  input.maxLength = 400;
  input.placeholder = 'この質問に先生が答える (質問した人と「知りたい」の人に届きます)';
  input.setAttribute('aria-label', `質問への回答: ${question.text}`);
  const submit = el('button', 'ls-secondary', '答える');
  submit.type = 'submit';
  form.append(input, submit);
  form.addEventListener('submit', (event) => {
    event.preventDefault();
    const text = input.value.trim();
    if (text && send({ type: 'questions.answer', id: question.id, text })) { input.value = ''; answerDrafts.delete(question.id); }
  });
  answerDrafts.set(question.id, form);
  return form;
}
function renderQuestions(questions) {
  const digest = JSON.stringify(questions.map(({ at, ...rest }) => rest));
  if (digest === questionsShown) return;
  questionsShown = digest;
  const order = (question) => question.status === 'pending' ? (question.needs_teacher ? 0 : 1) : question.status === 'shared' ? 2 : 3;
  const sorted = [...questions].sort((a, b) => order(a) - order(b) || b.score + 8 * b.votes - (a.score + 8 * a.votes));
  $('questions').replaceChildren(...sorted.map((question) => {
    const item = el('li');
    item.dataset.question = question.id;
    item.append(el('p', 'ls-q', question.text), el('p', 'ls-a', question.answer));
    const meta = el('div', 'ls-meta');
    meta.append(el('span', '', nameOf(question.student)), el('span', '', `評価 ${question.score}`), el('span', '', `知りたい ${question.votes}`));
    if (question.needs_teacher && question.status === 'pending') { const tag = el('span', 'co-tag', '先生の回答待ち'); tag.dataset.tone = 'warn'; meta.append(tag); }
    if (question.answered_by_teacher) { const tag = el('span', 'co-tag', '先生が回答済み'); tag.dataset.tone = 'good'; meta.append(tag); }
    if (question.status === 'pending') {
      meta.append(button('共有', 'ls-secondary', () => send({ type: 'questions.share', id: question.id })),
        button('見送る', 'ls-secondary', () => send({ type: 'questions.dismiss', id: question.id })));
    } else meta.append(el('span', 'co-tag', question.status === 'shared' ? '共有済み' : '見送り'));
    item.append(meta);
    if (question.status !== 'dismissed' && (question.needs_teacher || question.status === 'pending')) item.append(answerField(question));
    else answerDrafts.delete(question.id);
    return item;
  }));
  if (questions.length === 0) $('questions').append(el('li', 'ls-note', 'まだ質問はありません。'));
}
function renderSnapshot(snapshot) {
  const player = snapshot.player;
  $('clock').textContent = `${clock(player.position_ms)} / ${clock(player.duration_ms)}`;
  $('player-state').textContent = { idle: '開始前', running: '進行中', paused: '一時停止', finished: '終了' }[player.state];
  $('progress').style.width = `${(player.position_ms / player.duration_ms) * 100}%`;
  $('start').disabled = player.state !== 'idle';
  $('pause').disabled = player.state !== 'running';
  $('resume').disabled = player.state !== 'paused';
  $('skip').disabled = player.state === 'idle';
  for (const node of $('sections').children) node.setAttribute('aria-current', String(node.dataset.section === player.section));
  $('entry-lock').checked = snapshot.class.entry_locked;
  $('demo-tool').hidden = !snapshot.class.demo_tool;
  if (!toolTimer && 'presenter_tool' in snapshot)
    $('tool-state').textContent = snapshot.presenter_tool ? '実演用の道具: 接続済み (実演の切替は自動)' : '実演用の道具: 未接続 (実演の切替は手動)';
  renderRoster(snapshot);
  renderActivity(snapshot.activity, snapshot.students.filter((student) => student.connected && student.standing !== 'ejected').length);
  $('assignment-state').textContent = snapshot.assignment ? `${snapshot.assignment.title}: ${snapshot.assignment.active ? '実施中' : '終了'}` : '';
  renderQuestions(snapshot.questions);
}

// The presenter's tool (the Studio on the machine that shows the demo) is linked with a
// single-use code, the same way a student's is.
let toolTimer = 0;
function showToolLink(message) {
  clearTimeout(toolTimer);
  $('tool-string').value = `http://127.0.0.1:${message.port}/v1/tool#${message.code}`;
  $('tool-string').hidden = false;
  $('tool-string').select();
  $('tool-state').textContent = `実演を映す機械の「授業」タブに貼り付けてください (${message.seconds} 秒有効、1 回だけ)。別の機械なら 127.0.0.1 を、その機械から見えるこの授業のアドレスに置き換えます。`;
  toolTimer = setTimeout(() => { toolTimer = 0; $('tool-string').hidden = true; $('tool-string').value = ''; }, message.seconds * 1000);
}

const handlers = {
  'tool.link': showToolLink,
  catalog: renderCatalog,
  'console.welcome': renderWelcome,
  snapshot: renderSnapshot,
  heard: (m) => log(INTENT[m.intent] ?? m.intent, `${nameOf(m.student)}: ${m.text}`, m.intent === 'disruption' ? 'danger' : m.intent === 'help' ? 'warn' : ''),
  attention: (m) => log('要対応', `${nameOf(m.student)}: ${REASON[m.reason] ?? m.reason}`, 'warn'),
  'conduct.event': (m) => log(STANDING[m.standing][0] || '注意', `${nameOf(m.student)}: ${m.reason}`, 'danger'),
  error: (m) => { $('status').textContent = ERRORS[m.code] ?? `操作できませんでした (${m.code})`; },
};

$('open').addEventListener('click', () => { selected = ''; send({ type: 'class.open', plot: $('plots').value }); });
$('plots').addEventListener('change', () => { $('prep').href = `/v1/prep.pptx?plot=${encodeURIComponent($('plots').value)}`; });
$('classes').addEventListener('change', () => { selected = $('classes').value; if (selected) send({ type: 'class.select', class: selected }); else send({ type: 'catalog' }); });
$('close').addEventListener('click', () => { if (confirm('授業を閉じます。参加中の全員が切断されます。')) { selected = ''; send({ type: 'class.close' }); } });
$('tool-link').addEventListener('click', () => send({ type: 'tool.link' }));
$('entry-lock').addEventListener('change', () => send({ type: 'entry.lock', locked: $('entry-lock').checked }));
for (const [id, type] of [['start', 'lesson.start'], ['pause', 'lesson.pause'], ['resume', 'lesson.resume'], ['skip', 'lesson.skip'],
  ['activity-close', 'activity.close'], ['activity-reveal', 'activity.reveal'], ['assignment-close', 'assignment.close'], ['share-best', 'questions.share']])
  $(id).addEventListener('click', () => send({ type }));
$('activity-open').addEventListener('click', () => send({ type: 'activity.open', id: $('activities').value }));
$('assignment-assign').addEventListener('click', () => send({ type: 'assignment.assign', id: $('assignments').value }));
$('diarized').addEventListener('submit', (event) => {
  event.preventDefault();
  const text = $('heard').value.trim();
  if (text) { send({ type: 'voice.diarized', speaker: $('speaker').value.trim() || 'spk_0', text }); $('heard').value = ''; }
});
