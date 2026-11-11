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
  not_your_question: '自分の質問だけに「まだ分からない」を送れます。',
  no_answer_yet: 'この質問はすでに先生の回答待ちです。',
  own_question: '自分の質問には「私も知りたい」を押せません。',
  unknown_question: 'その質問はもうありません。',
};

let connection = null;
let code = '';
let name = '';
let activity = null;       // current prompt, or null
let assignment = null;
let lockTimer = null;
let assignmentActive = false;
const myQuestions = new Set();      // question ids this student asked (told by the server)
const votedQuestions = new Set();   // question ids this student said "me too" to (told by the server)
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
// A new section replaces everything the previous one put on screen. Its own slide and first line
// arrive right after, so nothing of the old section stays next to the new title.
function enterSection(message) {
  $('section-title').textContent = `${message.index + 1} / ${message.total}  ${message.title}`;
  $('demo').hidden = true;
  $('demo').textContent = '';
  $('slide').hidden = true;
  $('slide-title').textContent = '';
  $('slide-points').replaceChildren();
  $('narration').textContent = '';
  activity = null;
  $('activity').hidden = true;
  if (!assignmentActive) { assignment = null; $('assignment').hidden = true; }
}
function showDemo(message) {
  $('demo').hidden = false;
  const what = message.note || message.feature;
  const watch = message.watch ? ` 見るところ: ${message.watch}` : '';
  $('demo').textContent = `先生の画面 (配信・教室のスクリーン) で実演中: ${what} (${message.enabled ? 'ON' : 'OFF'})。${watch}`;
}

// ---- private feed ----
// A private message must be noticed by a student who is busy answering, without taking the
// keyboard away: the list is a live region, and a note at the bottom of the screen says how many
// arrived until the student scrolls to them or presses the note.
let unseenPrivate = 0;
let privateVisible = false;
function privateNotice() {
  const notice = $('private-notice');
  if (unseenPrivate === 0 || privateVisible) {
    unseenPrivate = 0;
    notice.hidden = true;
    for (const item of $('private').querySelectorAll('.ls-new')) item.classList.remove('ls-new');
    return;
  }
  const first = $('private').firstElementChild;
  const preview = first ? [...first.childNodes].filter((node) => node.nodeType === Node.TEXT_NODE).map((node) => node.textContent).join('').slice(0, 40) : '';
  notice.textContent = `先生からあなたへ 新着 ${unseenPrivate} 件: ${preview}${preview.length >= 40 ? '…' : ''} (押すと表示)`;
  notice.hidden = false;
}
function addPrivate(kind, text) {
  const list = $('private');
  if (list.firstElementChild?.classList.contains('ls-note')) list.replaceChildren();
  const item = el('li');
  item.append(el('span', 'ls-kind', kind), document.createTextNode(text));
  list.prepend(item);
  while (list.children.length > PRIVATE_KEEP) list.lastElementChild.remove();
  if (!privateVisible) { item.classList.add('ls-new'); unseenPrivate += 1; }
  privateNotice();
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
  $('activity-personal').hidden = true;
  showMine(prompt.answer ?? null);
  setActivityOpen(prompt.state === 'open', prompt.answered ? '回答済みです。締め切りまで変更できます。' : '');
}
// The answer the server holds for this student, shown as it is, so the page never claims a
// different one. Choices are re-checked from it; text goes back into the field.
function showMine(answer) {
  const mine = $('activity-mine');
  if (!answer || !activity) { mine.hidden = true; mine.textContent = ''; return; }
  if (answer.choices) {
    const chosen = answer.choices.map((index) => activity.choices?.[index]).filter(Boolean);
    for (const input of $('activity-choices').querySelectorAll('input')) input.checked = answer.choices.includes(Number(input.value));
    mine.textContent = `いまのあなたの回答: ${chosen.join('、')}`;
  } else {
    $('activity-text').value = answer.text;
    mine.textContent = `いまのあなたの回答: ${answer.text}`;
  }
  mine.hidden = false;
}
function acknowledge(message) {
  if (!activity || message.activity_id !== activity.id) return;
  showMine(message.answer);
  if (message.via === 'utterance') {
    $('activity-state').textContent = '「先生に話す」で送った言葉を、この問いへのあなたの回答として受け取りました。締め切りまで、ここで書き直せます。';
    toast('話した言葉を回答として受け取りました');
  } else $('activity-state').textContent = '受け取りました。締め切りまで変更できます。';
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
  answer.textContent = results.has_answer ? results.model_answer ?? '' : 'この質問に正解はありません。';
  answer.hidden = answer.textContent === '';
  $('activity-explanation').textContent = results.explanation ? `解説: ${results.explanation}` : '';
  $('activity-explanation').hidden = !results.explanation;
  $('activity-highlights').replaceChildren(...(results.highlights ?? []).map((text) => el('li', '', text)));
  setActivityOpen(false, `${results.answered} 人が答えました。`);
}
function renderPersonal(result) {
  $('activity-personal').hidden = false;
  const graded = result.text ?? (result.choices && activity ? result.choices.map((index) => activity.choices?.[index]).join('、') : '');
  $('activity-personal').textContent = graded ? `あなたの回答「${graded}」について: ${result.feedback}` : result.feedback;
  speak(result.feedback);
}

// ---- assignment ----
function renderAssignment(data) {
  assignment = data;
  assignmentActive = true;
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
      // The box shows what was asked; the server's progress (or its refusal) sets it for good.
      if (!send({ type: 'progress', assignment_id: data.id, step: index, done: input.checked })) input.checked = !input.checked;
    });
    const words = el('span', '', step.label);
    row.append(input, words);
    if (step.auto) {
      row.append(el('span', 'ls-auto', '自動'));
      if (step.check) words.append(el('span', 'ls-check', `自動: 道具が ${step.check.variable} ${OPS[step.check.op] ?? step.check.op} ${JSON.stringify(step.check.value)} を見たらチェック`));
    }
    item.append(row);
    // The exact way to do the step (commands to copy), written by the author.
    if (step.how) item.append(el('pre', 'ls-how', step.how));
    return item;
  }));
  $('assignment-status').textContent = '';
  $('help').disabled = false;
}
const OPS = { gt: '>', ge: '>=', lt: '<', le: '<=', eq: '=', ne: '!=' };
function closeAssignment() {
  assignmentActive = false;
  $('assignment-status').textContent = '課題の時間は終わりました。';
  $('help').disabled = true;
  for (const input of $('assignment-steps').querySelectorAll('input')) input.disabled = true;
}
function progressRejected(message) {
  // Put every box back to what the server holds; with no record for this assignment, undo all ticks.
  const boxes = $('assignment-steps').querySelectorAll('input');
  const done = message.assignment && assignment && message.assignment.id === assignment.id ? message.assignment.done : [];
  boxes.forEach((box, index) => { box.checked = Boolean(done[index]); });
  toast(ERROR_TEXT[message.code] ?? '進み具合を受け付けられませんでした。');
  if (message.code === 'not_assigned' && assignmentActive) closeAssignment();
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
    $('tool-state').textContent = `道具の「授業」タブに貼り付けてください (窓の無い Ergo Studio は POST /lesson/api/connect に {"link": "…"})。あと ${left} 秒、1 回だけ使えます。`;
    left -= 1;
  };
  tick();
  toolTimer = setInterval(tick, 1000);
}

// A tool that took the code is reported by the server (tool.linked, with the tool's own name kept to a
// safe form); the countdown stops and the linked state stays until the link ends.
function toolLinked(message) {
  clearInterval(toolTimer);
  $('tool-box').hidden = true;
  $('tool-string').value = '';
  const names = message.tools.join('、');
  $('tool-linked-text').textContent = `道具がつながりました: ${names}${message.count > 1 ? ` (${message.count} 台)` : ''}。「自動」の手順は道具が見てチェックします。`;
  $('tool-linked').hidden = false;
  $('tool-state').textContent = '';
  toast('道具がつながりました');
}
function toolUnlinked(message) {
  const wasLinked = !$('tool-linked').hidden;
  $('tool-linked').hidden = true;
  if (wasLinked || message.reason) {
    $('tool-state').textContent = message.reason === 'student'
      ? '道具の接続を切りました。つなぎ直すときは接続文字列を出し直してください。'
      : '道具の接続が切れました。つなぎ直すときは接続文字列を出し直してください。';
    toast('道具の接続が切れました');
  }
}

// ---- shared questions ----
// Counts shown here are only ever the server's. The asker is not offered "私も知りたい" on their
// own question; a vote is shown as given once the server confirms it.
function renderVote(item) {
  const id = item.dataset.question;
  const button = item.querySelector('button');
  if (!button) return;
  const mine = myQuestions.has(id);
  button.hidden = mine;
  button.disabled = mine || votedQuestions.has(id) || button.dataset.pending === 'true';
  button.textContent = votedQuestions.has(id) ? '知りたいを押しました' : '私も知りたい';
  item.querySelector('.ls-mine-tag')?.toggleAttribute('hidden', !mine);
}
function sharedItem(id) { return [...$('shared').children].find((item) => item.dataset.question === id) ?? null; }
function addShared(question) {
  $('shared-empty')?.remove();
  if (question.mine) myQuestions.add(question.id);
  if (question.voted) votedQuestions.add(question.id);
  const item = el('li');
  item.dataset.question = question.id;
  item.append(el('p', 'ls-q', question.text), el('p', 'ls-a', question.answer_from === 'none' ? 'まだ答えはありません (先生の回答待ち)' : question.answer_from === 'teacher' ? `先生の答え: ${question.answer}` : question.answer));
  const meta = el('div', 'ls-meta');
  if (question.name) meta.append(el('span', '', question.name));
  meta.append(el('span', 'ls-votes', `知りたい ${question.votes}`));
  const tag = el('span', 'ls-mine-tag', 'あなたの質問');
  tag.hidden = true;
  const button = el('button', 'ls-secondary', '私も知りたい');
  button.type = 'button';
  button.addEventListener('click', () => { if (send({ type: 'question.vote', id: question.id })) { button.dataset.pending = 'true'; button.disabled = true; } });
  meta.append(tag, button);
  item.append(meta);
  const existing = sharedItem(question.id);
  if (existing) existing.replaceWith(item); else $('shared').prepend(item);
  renderVote(item);
}
function questionReply(message) {
  if (message.relation === 'asker') myQuestions.add(message.question_id);
  if (message.relation === 'me_too') votedQuestions.add(message.question_id);
  const item = sharedItem(message.question_id);
  if (item) renderVote(item);
  addPrivate(message.from === 'teacher' ? '先生の答え' : '質問の答え', message.text);
}
function voted(message) {
  votedQuestions.add(message.id);
  const item = sharedItem(message.id);
  if (item) { delete item.querySelector('button')?.dataset.pending; renderVote(item); }
}
function votesChanged(message) {
  const item = sharedItem(message.id);
  if (item) item.querySelector('.ls-votes').textContent = `知りたい ${message.votes}`;
}

// ---- what was said to the teacher ----
// Every utterance comes back with how it was taken; the line under the form keeps it (with the
// start of what was said) until the next one, so it is not lost when the toast fades.
let lastSaid = '';
function heardBack(message) {
  const note = message.note ?? '聞こえました';
  const said = lastSaid ? `「${lastSaid.slice(0, 30)}${lastSaid.length > 30 ? '…' : ''}」→ ` : '';
  $('say-state').textContent = `${said}${note}`;
  $('say-state').dataset.takenAs = message.taken_as ?? '';
  toast(note);
}

// ---- this student's own questions ----
// Each question the student asked (or said "me too" to) with where it stands, so a question passed to
// the human teacher does not simply vanish. States come only from the server (question.status).
const QUESTION_STATE = { answered: '答えあり (用意した答え)', waiting: '先生の回答待ち', teacher: '先生から回答', dismissed: '見送り' };
const ownQuestions = new Map();
function renderOwnQuestions() {
  const list = $('my-questions');
  if (ownQuestions.size === 0) { list.replaceChildren(el('li', 'ls-note', 'まだありません。')); return; }
  list.replaceChildren(...[...ownQuestions.values()].reverse().map((question) => {
    const item = el('li');
    item.dataset.question = question.id;
    item.dataset.state = question.state;
    item.append(el('p', 'ls-q', question.text));
    if (question.answer) item.append(el('p', 'ls-a', question.answer));
    const meta = el('div', 'ls-meta');
    const state = el('span', 'co-tag ls-qstate', QUESTION_STATE[question.state] ?? question.state);
    if (question.state === 'waiting') state.dataset.tone = 'warn';
    if (question.state === 'dismissed') state.dataset.tone = 'danger';
    meta.append(state);
    if (question.relation === 'me_too') meta.append(el('span', '', '「知りたい」を押した質問'));
    if (question.shared) meta.append(el('span', '', 'クラスに共有済み'));
    if (question.unresolved && question.state === 'waiting') meta.append(el('span', '', '「まだ分からない」を送りました'));
    if (question.relation === 'asker' && (question.state === 'answered' || question.state === 'teacher')) {
      const button = el('button', 'ls-secondary', 'まだ分からない');
      button.type = 'button';
      button.addEventListener('click', () => { if (send({ type: 'question.unresolved', id: question.id })) button.disabled = true; });
      meta.append(button);
    }
    item.append(meta);
    return item;
  }));
}
function questionStatus(question) {
  ownQuestions.set(question.id, question);
  if (question.relation === 'asker') myQuestions.add(question.id);
  renderOwnQuestions();
}
// ---- this student's can't-follow reports ----
// A report that the class could not be followed is not a question, but it must not vanish either: the page
// shows whether the human teacher has seen it (states only from the server, report.status).
const REPORT_STATE = { reported: '知らせました (先生の確認待ち)', acknowledged: '先生が確認しました', replied: '先生から返事' };
const ownReports = new Map();
function renderOwnReports() {
  $('my-reports-box').hidden = ownReports.size === 0;
  $('my-reports').replaceChildren(...[...ownReports.values()].reverse().map((report) => {
    const item = el('li');
    item.dataset.report = report.id;
    item.dataset.state = report.state;
    item.append(el('p', 'ls-q', report.text));
    if (report.reply) item.append(el('p', 'ls-a', `先生から: ${report.reply}`));
    const meta = el('div', 'ls-meta');
    const state = el('span', 'co-tag ls-rstate', REPORT_STATE[report.state] ?? report.state);
    state.dataset.tone = report.state === 'reported' ? 'warn' : 'good';
    meta.append(state);
    item.append(meta);
    return item;
  }));
}
function reportStatus(report) {
  const before = ownReports.get(report.id)?.state;
  ownReports.set(report.id, report);
  renderOwnReports();
  if (before === 'reported' && report.state === 'acknowledged') toast('先生があなたの知らせを確認しました');
}

function questionRemoved(message) {
  sharedItem(message.id)?.remove();
  if ($('shared').children.length === 0) { $('shared').replaceChildren(el('li', 'ls-note', 'まだありません。')); $('shared').firstElementChild.id = 'shared-empty'; }
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
  activity = null;
  if (state.activity) renderActivity(state.activity); else $('activity').hidden = true;
  if (state.activity_results) renderResults(state.activity_results);
  if (state.activity_personal) renderPersonal(state.activity_personal);
  assignmentActive = false;
  if (state.assignment) renderAssignment(state.assignment); else $('assignment').hidden = true;
  $('tool').hidden = !message.lesson.tool;
  $('tool-linked').hidden = true;   // the server follows the welcome with tool.linked when a tool is still linked
  $('tool-engine').textContent = message.lesson.engine || '道具';
  $('shared').replaceChildren(el('li', 'ls-note', 'まだありません。'));
  $('shared').firstElementChild.id = 'shared-empty';
  for (const question of state.questions) addShared(question);
  ownQuestions.clear();
  for (const question of state.my_questions ?? []) ownQuestions.set(question.id, question);
  renderOwnQuestions();
  ownReports.clear();
  for (const report of state.my_reports ?? []) ownReports.set(report.id, report);
  renderOwnReports();
  showConduct(state.conduct);
}

const REPLY_KIND = { teacher: '先生から', help: '返事', trouble: '先生に知らせました', questions_unanswered: '答えられなかった質問',
  report: '先生から', reports_unacknowledged: '確認されなかった知らせ' };
const handlers = {
  welcome: applyWelcome,
  section: enterSection,
  slide: showSlide,
  narration: (m) => showNarration(m, true),
  demo: showDemo,
  'activity.open': (m) => { $('activity-text').value = ''; renderActivity(m.activity); $('activity').scrollIntoView({ behavior: 'smooth', block: 'nearest' }); },
  'activity.closed': () => setActivityOpen(false),
  'activity.reveal': (m) => renderResults(m.results),
  'activity.personal': (m) => renderPersonal(m.result),
  'answer.ack': acknowledge,
  assignment: (m) => renderAssignment(m.assignment),
  'assignment.closed': closeAssignment,
  'assignment.status': (m) => { $('assignment-status').textContent = `${m.total} 人中 ${m.done} 人が終わりました。`; },
  progress: (m) => updateProgress(m.assignment),
  'progress.rejected': progressRejected,
  'tool.link': showToolLink,
  'tool.linked': toolLinked,
  'tool.unlinked': toolUnlinked,
  hint: (m) => addPrivate(m.kind === 'class_hint' ? '全員へ' : m.kind === 'praise' ? 'できました' : 'ヒント', m.text),
  reply: (m) => (m.about === 'question' ? questionReply(m) : addPrivate(REPLY_KIND[m.about] ?? '返事', m.text)),
  'question.shared': (m) => addShared(m.question),
  'question.status': (m) => questionStatus(m.question),
  'question.removed': questionRemoved,
  'report.status': (m) => reportStatus(m.report),
  'question.voted': voted,
  'question.votes': votesChanged,
  conduct: showConduct,
  'utterance.ack': heardBack,
  'voice.enroll': (m) => { $('enroll-code').textContent = m.code; },
  'voice.enrolled': () => { $('enroll-code').textContent = '登録できました'; },
  'lesson.end': () => { $('section-title').textContent = '授業は終わりました'; showNarration({ text: 'おつかれさまでした。' }, false); },
  'class.closed': () => { stopped = true; connection?.stop(); $('status').textContent = '授業は閉じられました'; },
  error: (m) => {
    const text = ERROR_TEXT[m.code] ?? '送信できませんでした。';
    // A refused vote: let the button be pressed again only if a vote is still possible.
    for (const button of $('shared').querySelectorAll('button[data-pending="true"]')) { delete button.dataset.pending; renderVote(button.closest('li')); }
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
  onUtterance: (text) => { if (send({ type: 'utterance', text: text.slice(0, 280) })) lastSaid = text; },
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
$('tool-unlink').addEventListener('click', () => send({ type: 'tool.unlink' }));
$('tool-copy').addEventListener('click', async () => {
  try { await navigator.clipboard.writeText($('tool-string').value); toast('コピーしました'); }
  catch { $('tool-string').select(); toast('選択しました。コピーしてください'); }
});
$('help').addEventListener('click', () => { if (assignment) send({ type: 'help', assignment_id: assignment.id }); });
$('say-form').addEventListener('submit', (event) => {
  event.preventDefault();
  const text = $('say').value.trim();
  if (text && send({ type: 'utterance', text })) { lastSaid = text; $('say').value = ''; }
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
$('private-notice').addEventListener('click', () => {
  $('private-card').scrollIntoView({ behavior: 'smooth', block: 'center' });
  unseenPrivate = 0;
  privateNotice();
});
if ('IntersectionObserver' in window) {
  new IntersectionObserver((entries) => {
    privateVisible = entries.some((entry) => entry.isIntersecting && (entry.intersectionRatio >= 0.5 || entry.intersectionRect.height >= window.innerHeight * 0.4));
    if (privateVisible) privateNotice();
  }, { threshold: [0, 0.25, 0.5, 0.75, 1] }).observe($('private-card'));
}

const fragment = new URLSearchParams(location.hash.slice(1));
$('code').value = fragment.get('code') ?? '';
$('name').value = stored(NAME_KEY);
