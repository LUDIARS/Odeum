// Actions on a student seat, done through the page's own controls (fill + click), so they look
// on the video exactly as a person would do them. Each action waits for the page to react and
// returns what the page showed, including its error toasts (locked, closed, ...).
const { readSeat } = require('./seat-dom.cjs');

const REACT_MS = 6000;   // how long an action waits for the page to show a reaction
const QUIET_MS = 1500;   // for actions the page does not confirm: how long to watch for an error toast

const fail = (code, text, extra = {}) => ({ ok: false, error: { source: 'harness', code, text }, ...extra });

async function view(page) {
  const seat = await page.evaluate(readSeat);
  if (seat.missing) throw new Error(`student page is missing elements: ${seat.missing.join(', ')}`);
  return seat;
}

async function focus(page, selector) {
  await page.evaluate((target) => document.querySelector(target)?.scrollIntoView({ block: 'center', behavior: 'smooth' }), selector);
  await page.waitForTimeout(400);
}

async function type(page, selector, text) {
  const field = page.locator(selector);
  await field.fill('');
  // Typed visibly, but never longer than about 3 s.
  await field.pressSequentially(text, { delay: Math.max(2, Math.min(25, Math.floor(3000 / Math.max(1, text.length)))) });
}

// Waits until one of the watched elements changed after `before` in a way that counts as a reaction.
// Returns the list of ids that reacted, or [] on timeout.
async function reaction(page, before, watch, timeout) {
  try {
    const handle = await page.waitForFunction(({ before: prior, watch: rules }) => {
      const hits = [];
      for (const rule of rules) {
        if ((window.__attend.mutations[rule.id] ?? 0) <= (prior[rule.id] ?? 0)) continue;
        const words = (document.getElementById(rule.id)?.textContent ?? '').trim();
        if (rule.nonEmpty && !words) continue;
        if (rule.includes && !words.includes(rule.includes)) continue;
        if (rule.exclude && rule.exclude.some((text) => words === text)) continue;
        hits.push(rule.id);
      }
      return hits.length ? hits : null;
    }, { before, watch }, { timeout, polling: 100 });
    return await handle.jsonValue();
  } catch (error) {
    if (error.name === 'TimeoutError') return [];
    throw error;
  }
}
const counters = (page) => page.evaluate(() => ({ ...window.__attend.mutations }));
const toast = (page) => page.evaluate(() => document.getElementById('feedback').textContent.trim());
const toastIsError = (page) => page.evaluate(() => document.getElementById('feedback').dataset.kind === 'error');

// The toast counts only when it changed after the action (`before`): a toast still showing from an earlier
// action is not this action's message.
async function freshToast(page, before) {
  return (await counters(page)).feedback > (before.feedback ?? 0) ? toast(page) : '';
}
// A fresh toast the page marks as an error is a refusal; an informational one (「私も知りたい」を受け付けました) is not.
async function outcome(page, before, hits) {
  const message = await freshToast(page, before);
  if (hits.includes('feedback') && message && await toastIsError(page)) return { ok: false, error: { source: 'page', text: message }, page_message: message };
  return { ok: true, reacted: hits, page_message: message || null };
}

async function answer(page, { text, choices }) {
  const seat = await view(page);
  if (!seat.activity) return fail('no_activity', 'いまは問いが出ていません (画面に問いがありません)。');
  if (seat.activity.state !== 'open') return fail('activity_closed', seat.activity.state_note || 'この問いは受付中ではありません。', { activity_state: seat.activity.state });
  await focus(page, '#activity');
  if (seat.activity.choices) {
    if (!choices) return fail('choices_required', 'この問いは選択式です。--choices で番号 (0 始まり) を渡してください。');
    const multi = seat.activity.input.includes('multi');
    if (!multi && choices.length !== 1) return fail('single_choice', 'この問いは 1 つだけ選べます。');
    for (const index of choices) if (!Number.isInteger(index) || index < 0 || index >= seat.activity.choices.length)
      return fail('bad_choice', `選択肢の番号は 0〜${seat.activity.choices.length - 1} です。`);
    const inputs = page.locator('#activity-choices input');
    for (let index = 0; index < seat.activity.choices.length; index += 1) {
      if (multi) await inputs.nth(index).setChecked(choices.includes(index));
      else if (choices.includes(index)) await inputs.nth(index).check();
    }
  } else {
    if (typeof text !== 'string' || !text.trim()) return fail('text_required', 'この問いは記述式です。--text で答えを渡してください。');
    await type(page, '#activity-text', text);
  }
  const before = await counters(page);
  await page.click('#activity-send');
  const hits = await reaction(page, before, [{ id: 'activity-state', includes: '受け取りました' }, { id: 'feedback', nonEmpty: true }], REACT_MS);
  if (!hits.length) return fail('no_response', '答えを送りましたが、画面に受け取りの表示が出ませんでした。');
  if (hits.includes('activity-state')) return { ok: true, acked: true, page_message: (await freshToast(page, before)) || null };
  // Without the page's 受け取りました the answer is not confirmed, whatever else the page said.
  const refused = await outcome(page, before, hits);
  return refused.ok ? fail('no_ack', '答えを送りましたが、画面に受け取りの表示が出ませんでした。', { page_message: refused.page_message }) : refused;
}

async function tick(page, { step, undo }) {
  const seat = await view(page);
  if (!seat.assignment) return fail('no_assignment', 'いまは課題が出ていません (画面に課題がありません)。');
  const entry = seat.assignment.steps.find((item) => item.n === step);
  if (!entry) return fail('bad_step', `手順の番号は 1〜${seat.assignment.steps.length} です。`);
  if (entry.done === !undo) return fail(undo ? 'not_done' : 'already_done', undo ? 'この手順にはチェックが付いていません。' : 'この手順にはもうチェックが付いています。');
  await focus(page, '#assignment');
  const before = await counters(page);
  await page.locator('#assignment-steps input').nth(step - 1).setChecked(!undo);
  const hits = await reaction(page, before, [{ id: 'feedback', nonEmpty: true }, { id: 'private' }], QUIET_MS);
  return outcome(page, before, hits);
}

async function help(page) {
  const seat = await view(page);
  if (!seat.assignment) return fail('no_assignment', '「ヒントがほしい」は課題が出ているときだけ画面にあります。');
  if (!seat.assignment.active) return fail('help_disabled', seat.assignment.status || '「ヒントがほしい」は押せない状態です。');
  await focus(page, '#assignment');
  const before = await counters(page);
  await page.click('#help');
  const hits = await reaction(page, before, [{ id: 'private' }, { id: 'feedback', nonEmpty: true }], REACT_MS);
  if (!hits.length) return fail('no_response', 'ヒントを求めましたが、画面に返事が出ませんでした。');
  if (hits.includes('private')) await focus(page, '#private');
  return outcome(page, before, hits);
}

const SENT_QUESTION = '送りました。返事は「先生からあなたへ」に届きます。';   // lesson.js SENT_TEXT
async function ask(page, { text, showName }) {
  if (typeof text !== 'string' || !text.trim()) return fail('text_required', '--text で質問を渡してください。');
  await focus(page, '#question-form');
  await type(page, '#question', text);
  await page.locator('#show-name').setChecked(Boolean(showName));
  const before = await counters(page);
  await page.click('#question-form button[type=submit]');
  const hits = await reaction(page, before, [{ id: 'private' }, { id: 'feedback', nonEmpty: true, exclude: [SENT_QUESTION] }], REACT_MS);
  if (!hits.length) return fail('no_response', '質問を送りましたが、画面に答えが出ませんでした。', { page_message: (await freshToast(page, before)) || null });
  if (hits.includes('private')) await focus(page, '#private');
  return outcome(page, before, hits);
}

async function say(page, { text }) {
  if (typeof text !== 'string' || !text.trim()) return fail('text_required', '--text で話す内容を渡してください。');
  await focus(page, '#say-form');
  await type(page, '#say', text);
  const before = await counters(page);
  await page.click('#say-form button[type=submit]');
  // The page answers every utterance with how it was taken (the line under the form); a refusal
  // (locked, not connected) shows only as a toast.
  const hits = await reaction(page, before, [{ id: 'say-state', nonEmpty: true }, { id: 'feedback', nonEmpty: true }, { id: 'conduct' }], REACT_MS);
  if (!hits.length) return fail('no_response', '送りましたが、画面に反応が出ませんでした。');
  if (!hits.includes('say-state')) {
    // The acknowledgement can follow a moment after a toast; give it a little more time.
    const late = await reaction(page, before, [{ id: 'say-state', nonEmpty: true }], QUIET_MS);
    if (!late.length) {
      const refused = await outcome(page, before, hits);
      return refused.ok ? fail('no_response', '送りましたが、どう受け取られたかが画面に出ませんでした。', { page_message: refused.page_message }) : refused;
    }
    hits.push(...late);
  }
  const said = await page.evaluate(() => ({ text: document.getElementById('say-state').textContent.trim(), taken_as: document.getElementById('say-state').dataset.takenAs || null }));
  // taken_as: answer (the open question's answer) / remark (already answered; the answer is kept) / question / help /
  // trouble (could not see / hear / follow) / chatter / disruption.
  return { ok: true, reacted: hits, page_message: said.text, taken_as: said.taken_as, taken_as_answer: said.taken_as === 'answer' };
}
async function vote(page, { n }) {
  const seat = await view(page);
  const item = seat.shared.find((question) => question.n === n);
  if (!item) return fail('bad_question', `共有された質問の番号 (n) は ${seat.shared.map((question) => question.n).join(', ') || 'まだありません'} です。`);
  if (item.mine) return fail('own_question', 'あなたの質問です。自分の質問には「私も知りたい」を押せません (ボタンが出ていません)。');
  if (!item.can_vote) return fail('already_voted', '「私も知りたい」はもう押してあります。');
  const button = page.locator(`#shared li[data-attend-shared="${n}"] button`);
  await button.scrollIntoViewIfNeeded();
  const before = await counters(page);
  await button.click();
  // The count changes only when the server confirms the vote.
  const hits = await reaction(page, before, [{ id: 'shared' }, { id: 'feedback', nonEmpty: true }], REACT_MS);
  if (!hits.length) return fail('no_response', '「私も知りたい」を押しましたが、画面に反応が出ませんでした。');
  return outcome(page, before, hits);
}

// 「まだ分からない」 on one of this student's own questions (n from observe's my_questions).
async function unresolved(page, { n }) {
  const seat = await view(page);
  const item = seat.my_questions.find((question) => question.n === n);
  if (!item) return fail('bad_question', `あなたの質問の番号 (n) は ${seat.my_questions.map((question) => question.n).join(', ') || 'まだありません'} です。`);
  if (!item.can_unresolve) return fail('not_available', `この質問には「まだ分からない」を押せません (いまの状態: ${item.state})。`);
  const button = page.locator(`#my-questions li[data-attend-mine="${n}"] button`);
  await button.scrollIntoViewIfNeeded();
  const before = await counters(page);
  await button.click();
  const hits = await reaction(page, before, [{ id: 'my-questions' }, { id: 'feedback', nonEmpty: true }], REACT_MS);
  if (!hits.length) return fail('no_response', '「まだ分からない」を押しましたが、画面に反応が出ませんでした。');
  return outcome(page, before, hits);
}

// 「接続文字列を出す」: returns the string a tool (Ergo Studio's 「授業」 tab or POST /lesson/api/connect) takes.
async function toolLink(page) {
  const seat = await view(page);
  if (!seat.tool) return fail('no_tool', 'この授業には道具をつなぐ欄がありません。');
  await focus(page, '#tool');
  const before = await counters(page);
  await page.click('#tool-link');
  const hits = await reaction(page, before, [{ id: 'tool-state', includes: '秒' }, { id: 'feedback', nonEmpty: true }], REACT_MS);
  if (!hits.length) return fail('no_response', '接続文字列を求めましたが、画面に出ませんでした。');
  if (!hits.includes('tool-state')) return outcome(page, before, hits);
  const link = await page.inputValue('#tool-string');
  if (!/\/v1\/tool#[0-9A-Z]{10}$/.test(link)) return fail('bad_link', '画面の接続文字列の形が想定と違います。');
  return { ok: true, link, note: (await page.textContent('#tool-state')).trim() };
}

module.exports = { view, focus, answer, tick, help, ask, say, vote, toolLink, unresolved };
