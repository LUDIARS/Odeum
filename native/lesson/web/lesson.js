// Student page: http://<service>/lesson#code=<join code>
// One socket carries everything: the lecturer's cues, this student's answers, progress and
// speech, and the lecturer's private replies. State survives a reconnect through the resume token.
import { connectWithBackoff, relaySocketUrl } from './socket.js';
import { createMicrophone } from './voice.js';

const NAME_KEY = 'odeum.lesson.name';
const RESUME_KEY = 'odeum.lesson.resume';
const PRIVATE_KEEP = 30;

const $ = (id) => document.getElementById(id);
const el = (tag, className, text) => {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
};
const stored = (key) => { try { return sessionStorage.getItem(key) ?? ''; } catch { return ''; } };
const store = (key, value) => { try { sessionStorage.setItem(key, value); } catch { /* storage unavailable */ } };

const ERROR_TEXT = {
  class_not_found: 'コードが違うか、授業がまだ開かれていません。',
  rate_limited: '少し待ってからもう一度試してください。',
  invalid_name: '表示名は 1〜32 文字で入力してください。',
  entry_locked: 'この授業は入室を締め切っています。先生に声をかけてください。',
  ejected: 'この授業からは退出になっています。',
  capacity: '満席です。先生に声をかけてください。',
  locked: 'いまは発言できません。課題は続けられます。',
  activity_closed: 'この質問は締め切られました。',
  invalid_answer: '回答を確認してください。',
  invalid_question: '質問は 1〜280 文字で送ってください。',
  not_assigned: 'いまは課題が出ていません。',
};

let connection = null;
let code = '';
let name = '';
let activity = null;       // current prompt, or null
let assignment = null;
let lockTimer = null;
let stopped = false;
let transcribe = false;

function toast(text) {
  $('feedback').textContent = text;
  if (text) setTimeout(() => { if ($('feedback').textContent === text) $('feedback').textContent = ''; }, 4000);
}
function send(message) {
  if (connection?.send(message)) return true;
  toast('接続していません。入力はそのまま残しています。');
  return false;
}
function speak(text) {
  if (!$('speak').checked || !('speechSynthesis' in window)) return;
  const utterance = new SpeechSynthesisUtterance(text);
  utterance.lang = 'ja-JP';
  window.speechSynthesis.speak(utterance);
}

// ---- lecturer ----
function showSlide(slide) {
  $('slide').hidden = false;
  $('slide-title').textContent = slide.title;
  $('slide-points').replaceChildren(...slide.points.map((point) => el('li', '', point)));
}
function showNarration(message, aloud) {
  $('narration').textContent = message.text;
  if (aloud) speak(message.text);
}

// ---- private feed ----
function addPrivate(kind, text) {
  const list = $('private');
  if (list.firstElementChild?.classList.contains('ls-note')) list.replaceChildren();
  const item = el('li');
  item.append(el('span', 'ls-kind', kind), document.createTextNode(text));
  list.prepend(item);
  while (list.children.length > PRIVATE_KEEP) list.lastElementChild.remove();
  speak(text);
}

// ---- activity ----
function renderActivity(prompt) {
  activity = prompt;
  $('activity').hidden = false;
  $('activity-prompt').textContent = prompt.prompt;
  $('activity-code').hidden = !prompt.code;
  $('activity-code').textContent = prompt.code ?? '';
  const choices = $('activity-choices');
  choices.replaceChildren();
  const isChoice = prompt.kind === 'choice';
  $('activity-text').hidden = isChoice;
  if (isChoice) {
    prompt.choices.forEach((label, index) => {
      const row = el('label', 'ls-choice');
      const input = el('input');
      input.type = prompt.multi ? 'checkbox' : 'radio';
      input.name = 'choice';
      input.value = String(index);
      row.append(input, document.createTextNode(label));
      choices.append(row);
    });
  }
  $('activity-results').hidden = true;
  setActivityOpen(prompt.state === 'open', prompt.answered ? '回答済みです。締め切りまで変更できます。' : '');
}
function setActivityOpen(open, note) {
  $('activity-send').disabled = !open;
  $('activity-text').disabled = !open;
  for (const input of $('activity-choices').querySelectorAll('input')) input.disabled = !open;
  $('activity-state').textContent = note ?? (open ? '' : '締め切りました。');
}
function bar(label, count, total, correct) {
  const row = el('div', 'ls-bar');
  if (correct) row.dataset.correct = 'true';
  const track = el('div', 'ls-bar-track');
  const fill = el('div', 'ls-bar-fill');
  fill.style.width = `${total > 0 ? Math.round((count / total) * 100) : 0}%`;
  track.append(fill);
  row.append(el('span', 'ls-bar-label', correct ? `${label} (正解)` : label), el('span', 'ls-bar-count', String(count)), track);
  return row;
}
function renderResults(results) {
  $('activity-results').hidden = false;
  const bars = $('activity-bars');
  bars.replaceChildren();
  if (results.kind === 'choice') {
    results.choices.forEach((label, index) => bars.append(bar(label, results.counts[index], results.answered, results.correct.includes(index))));
  } else {
    for (const point of results.points) bars.append(bar(point.label, point.count, results.answered, false));
  }
  const answer = $('activity-answer');
  answer.textContent = results.has_answer ? results.model_answer ?? '' : 'この質問に正解はありません。理由を言えることが大事です。';
  answer.hidden = answer.textContent === '';
  $('activity-highlights').replaceChildren(...(results.highlights ?? []).map((text) => el('li', '', text)));
  setActivityOpen(false, `${results.answered} 人が答えました。`);
}
function renderPersonal(result) {
  $('activity-personal').hidden = false;
  $('activity-personal').textContent = result.feedback;
  speak(result.feedback);
}

// ---- assignment ----
function renderAssignment(data) {
  assignment = data;
  $('assignment').hidden = false;
  $('assignment-title').textContent = data.title;
  $('assignment-brief').textContent = data.brief;
  const done = data.done ?? [];
  $('assignment-steps').replaceChildren(...data.steps.map((step, index) => {
    const item = el('li');
    const row = el('label');
    const input = el('input');
    input.type = 'checkbox';
    input.checked = Boolean(done[index]);
    input.addEventListener('change', () => {
      if (!send({ type: 'progress', assignment_id: data.id, step: index, done: input.checked })) input.checked = !input.checked;
    });
    row.append(input, document.createTextNode(step.label));
    if (step.auto) row.append(el('span', 'ls-auto', '自動'));
    item.append(row);
    return item;
  }));
  $('assignment-status').textContent = '';
  $('help').disabled = false;
}
function updateProgress(progress) {
  const boxes = $('assignment-steps').querySelectorAll('input');
  progress.done.forEach((value, index) => { if (boxes[index]) boxes[index].checked = value; });
}

// ---- tool on this machine ----
// The code is single-use and short-lived; it is shown here and nowhere else. The tool trades it
// for its own access, so this page never sees what the tool holds.
let toolTimer = 0;
function showToolLink(message) {
  clearInterval(toolTimer);
  let left = message.seconds;
  $('tool-string').value = `${location.origin}/v1/tool#${message.code}`;
  $('tool-box').hidden = false;
  const tick = () => {
    if (left <= 0) { clearInterval(toolTimer); $('tool-box').hidden = true; $('tool-string').value = ''; $('tool-state').textContent = '期限が切れました。もう一度出してください。'; return; }
    $('tool-state').textContent = `「授業」タブに貼り付けてください。あと ${left} 秒、1 回だけ使えます。`;
    left -= 1;
  };
  tick();
  toolTimer = setInterval(tick, 1000);
}

// ---- shared questions ----
function addShared(question) {
  $('shared-empty')?.remove();
  const item = el('li');
  item.append(el('p', 'ls-q', question.text), el('p', 'ls-a', question.answer));
  const meta = el('div', 'ls-meta');
  if (question.name) meta.append(el('span', '', question.name));
  const votes = el('span', '', `知りたい ${question.votes}`);
  const button = el('button', 'ls-secondary', '私も知りたい');
  button.type = 'button';
  button.addEventListener('click', () => { if (send({ type: 'question.vote', id: question.id })) { button.disabled = true; votes.textContent = `知りたい ${question.votes + 1}`; } });
  meta.append(votes, button);
  item.append(meta);
  $('shared').prepend(item);
}

// ---- conduct ----
function showConduct(message) {
  const banner = $('conduct');
  if (lockTimer) clearInterval(lockTimer);
  lockTimer = null;
  banner.dataset.standing = message.standing;
  if (message.standing === 'good') { banner.hidden = true; return; }
  banner.hidden = false;
  const reason = message.reason ? `${message.reason}。` : '';
  if (message.standing === 'warned') { banner.textContent = `${reason}授業に集中しましょう。続くと発言できなくなります。`; return; }
  if (message.standing === 'ejected') {
    banner.textContent = message.reason ? reason : 'この授業からは退出になりました。';
    stopped = true;
    microphone.stop();
    connection?.stop();
    return;
  }
  const until = Date.now() + message.remaining_ms;
  const tick = () => {
    const seconds = Math.max(0, Math.ceil((until - Date.now()) / 1000));
    banner.textContent = `${reason}あと ${seconds} 秒、発言と回答ができません。授業を聞くことと課題は続けられます。`;
  };
  tick();
  lockTimer = setInterval(tick, 1000);
  microphone.stop();
  $('mic').setAttribute('aria-pressed', 'false');
}

// ---- messages ----
function applyWelcome(message) {
  store(RESUME_KEY, message.resume);
  transcribe = message.stt === 'browser';
  $('entry').hidden = true;
  $('stage').hidden = false;
  $('lesson-title').textContent = message.lesson.title;
  $('prep').href = `/v1/prep.pptx?plot=${encodeURIComponent(message.lesson.id)}`;
  const state = message.state;
  const current = message.lesson.sections.findIndex((section) => section.id === state.section);
  $('section-title').textContent = current >= 0 ? `${current + 1} / ${message.lesson.sections.length}  ${message.lesson.sections[current].title}` : 'まもなく始まります';
  if (state.slide) showSlide(state.slide);
  if (state.narration) showNarration(state.narration, false);
  if (state.activity) renderActivity(state.activity); else $('activity').hidden = true;
  if (state.activity_results) renderResults(state.activity_results);
  if (state.activity_personal) renderPersonal(state.activity_personal);
  if (state.assignment) renderAssignment(state.assignment); else $('assignment').hidden = true;
  $('tool').hidden = !message.lesson.tool;
  $('tool-engine').textContent = message.lesson.engine || '道具';
  $('shared').replaceChildren(el('li', 'ls-note', 'まだありません。'));
  $('shared').firstElementChild.id = 'shared-empty';
  for (const question of state.questions) addShared(question);
  showConduct(state.conduct);
}

const handlers = {
  welcome: applyWelcome,
  section: (m) => { $('section-title').textContent = `${m.index + 1} / ${m.total}  ${m.title}`; $('demo').hidden = true; },
  slide: showSlide,
  narration: (m) => showNarration(m, true),
  demo: (m) => { $('demo').hidden = false; $('demo').textContent = `画面で実演: ${m.note || m.feature} (${m.enabled ? 'ON' : 'OFF'})`; },
  'activity.open': (m) => { renderActivity(m.activity); $('activity-text').value = ''; $('activity-personal').hidden = true; $('activity').scrollIntoView({ behavior: 'smooth', block: 'nearest' }); },
  'activity.closed': () => setActivityOpen(false),
  'activity.reveal': (m) => renderResults(m.results),
  'activity.personal': (m) => renderPersonal(m.result),
  'answer.ack': () => { $('activity-state').textContent = '受け取りました。締め切りまで変更できます。'; },
  assignment: (m) => renderAssignment(m.assignment),
  'assignment.closed': () => { $('assignment-status').textContent = '課題の時間は終わりました。'; $('help').disabled = true; },
  'assignment.status': (m) => { $('assignment-status').textContent = `${m.total} 人中 ${m.done} 人が終わりました。`; },
  progress: (m) => updateProgress(m.assignment),
  'tool.link': showToolLink,
  hint: (m) => addPrivate(m.kind === 'class_hint' ? '全員へ' : m.kind === 'praise' ? 'できました' : 'ヒント', m.text),
  reply: (m) => addPrivate(m.about === 'question' ? '質問の答え' : m.about === 'teacher' ? '先生から' : '返事', m.text),
  'question.shared': (m) => addShared(m.question),
  conduct: showConduct,
  'utterance.ack': () => toast('聞こえました'),
  'voice.enroll': (m) => { $('enroll-code').textContent = m.code; },
  'voice.enrolled': () => { $('enroll-code').textContent = '登録できました'; },
  'lesson.end': () => { $('section-title').textContent = '授業は終わりました'; showNarration({ text: 'おつかれさまでした。' }, false); },
  'class.closed': () => { stopped = true; connection?.stop(); $('status').textContent = '授業は閉じられました'; },
  error: (m) => {
    const text = ERROR_TEXT[m.code] ?? '送信できませんでした。';
    if ($('stage').hidden) { $('entry-status').textContent = text; if (['ejected', 'entry_locked', 'invalid_name', 'capacity'].includes(m.code)) { stopped = true; connection?.stop(); } }
    else toast(text);
  },
};

function join() {
  stopped = false;
  connection?.stop();
  connection = connectWithBackoff({
    url: () => {
      const params = { code, name };
      const resume = stored(RESUME_KEY);
      if (resume) params.resume = resume;
      return relaySocketUrl('/v1/join', params);
    },
    onMessage: (message) => handlers[message.type]?.(message),
    onState: (state) => {
      if (stopped) return;
      const text = { connecting: '接続中…', open: '接続中', waiting: '再接続を待っています…' }[state];
      if (text) { $('status').textContent = text; if ($('stage').hidden) $('entry-status').textContent = state === 'open' ? '' : text; }
    },
  });
}

const microphone = createMicrophone({
  onLevel: (level) => { $('meter').style.width = `${level}%`; connection?.send({ type: 'voice.level', level }); },
  onUtterance: (text) => send({ type: 'utterance', text: text.slice(0, 280) }),
  onState: (text) => { $('mic-state').textContent = text; },
  get transcribe() { return transcribe; },
});

$('entry-form').addEventListener('submit', (event) => {
  event.preventDefault();
  code = $('code').value.trim();
  name = $('name').value.trim();
  if (!code || !name) return;
  store(NAME_KEY, name);
  join();
});
$('activity-form').addEventListener('submit', (event) => {
  event.preventDefault();
  if (!activity) return;
  if (activity.kind === 'choice') {
    const choices = [...$('activity-choices').querySelectorAll('input:checked')].map((input) => Number(input.value));
    if (choices.length === 0) { toast('選んでから答えてください。'); return; }
    send({ type: 'answer', activity_id: activity.id, choices });
    return;
  }
  const text = $('activity-text').value.trim();
  if (!text) { toast('答えを書いてから送ってください。'); return; }
  send({ type: 'answer', activity_id: activity.id, text });
});
$('tool-link').addEventListener('click', () => send({ type: 'tool.link' }));
$('tool-copy').addEventListener('click', async () => {
  try { await navigator.clipboard.writeText($('tool-string').value); toast('コピーしました'); }
  catch { $('tool-string').select(); toast('選択しました。コピーしてください'); }
});
$('help').addEventListener('click', () => { if (assignment) send({ type: 'help', assignment_id: assignment.id }); });
$('say-form').addEventListener('submit', (event) => {
  event.preventDefault();
  const text = $('say').value.trim();
  if (text && send({ type: 'utterance', text })) $('say').value = '';
});
$('question-form').addEventListener('submit', (event) => {
  event.preventDefault();
  const text = $('question').value.trim();
  if (text && send({ type: 'question', text, show_name: $('show-name').checked })) { $('question').value = ''; toast('質問を送りました。答えは「先生からあなたへ」に届きます。'); }
});
$('mic').addEventListener('click', async () => {
  const on = await microphone.toggle();
  $('mic').setAttribute('aria-pressed', String(on));
  $('mic').textContent = on ? 'マイクを止める' : 'マイクを使う';
});
$('enroll').addEventListener('click', () => send({ type: 'voice.enroll' }));

const fragment = new URLSearchParams(location.hash.slice(1));
$('code').value = fragment.get('code') ?? '';
$('name').value = stored(NAME_KEY);
