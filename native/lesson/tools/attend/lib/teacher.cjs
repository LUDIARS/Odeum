// Scripted human teacher (--teacher-answers file.json): answers questions that wait for the teacher
// (先生の回答待ち on the console) with answers written beforehand by the course author, through the
// console's own answer field and 答える button, so the operator-answer path is on the video.
//
// File: {"answers": [{"id": "...", "all": ["word", ...], "any": ["word", ...], "answer": "..."}]}
//   all: every word must appear in the question; any (optional): at least one must. Matching ignores case.
//   The first matching entry is used. A question nothing matches is left waiting (as a busy teacher would).
//   A question that waits again after an answer (the student pressed まだ分からない) gets the next
//   matching entry not used for it yet, or is left waiting.
const fs = require('fs');
const { exclusive } = require('./operator.cjs');

const POLL_MS = 2000;

function loadAnswers(file) {
  const parsed = JSON.parse(fs.readFileSync(file, 'utf8'));
  if (!Array.isArray(parsed.answers) || parsed.answers.length === 0) throw new Error(`${file}: expected {"answers": [...]}`);
  return parsed.answers.map((entry, index) => {
    const where = `${file}: answers[${index}]`;
    if (typeof entry.id !== 'string' || !entry.id) throw new Error(`${where}: id is required`);
    if (!Array.isArray(entry.all) || entry.all.length === 0 || !entry.all.every((word) => typeof word === 'string' && word)) throw new Error(`${where}: all must list words`);
    if (entry.any !== undefined && (!Array.isArray(entry.any) || !entry.any.every((word) => typeof word === 'string' && word))) throw new Error(`${where}: any must list words`);
    if (typeof entry.answer !== 'string' || !entry.answer.trim() || [...entry.answer].length > 400) throw new Error(`${where}: answer must be 1-400 characters`);
    return { id: entry.id, all: entry.all.map((word) => word.toLowerCase()), any: (entry.any ?? []).map((word) => word.toLowerCase()), answer: entry.answer.trim() };
  });
}

function match(answers, question, used = []) {
  const text = question.toLowerCase();
  return answers.find((entry) => !used.includes(entry.id) && entry.all.every((word) => text.includes(word))
    && (entry.any.length === 0 || entry.any.some((word) => text.includes(word)))) ?? null;
}

function createTeacher({ page, answers, delaySeconds, log, onError }) {
  const seen = new Map();    // question id -> when it was first seen waiting
  const left = new Set();    // waiting with nothing (more) to say: logged once
  const used = new Map();    // question id -> entry ids already given
  let timer = null;
  let stopped = false;

  const waiting = () => page.evaluate(() => [...document.querySelectorAll('#questions li[data-question]')]
    .filter((item) => [...item.querySelectorAll('.co-tag')].some((tag) => tag.textContent.trim() === '先生の回答待ち') && item.querySelector('form input'))
    .map((item) => ({ id: item.dataset.question, text: item.querySelector('.ls-q').textContent.trim() })));

  async function answerOne(question, entry) {
    const field = page.locator(`#questions li[data-question="${question.id}"] form input`);
    await field.scrollIntoViewIfNeeded();
    await field.fill('');
    await field.pressSequentially(entry.answer, { delay: 4 });
    await page.locator(`#questions li[data-question="${question.id}"] form button[type=submit]`).click();
    // Done when the console shows it answered and no longer waiting (a re-wait carries both tags).
    await page.waitForFunction((id) => {
      const tags = [...document.querySelectorAll(`#questions li[data-question="${id}"] .co-tag`)].map((tag) => tag.textContent.trim());
      return tags.includes('先生が回答済み') && !tags.includes('先生の回答待ち');
    },
      question.id, { timeout: 8000, polling: 200 });
  }

  async function poll() {
    if (stopped) return;
    try {
      await exclusive(async () => {
        const now = await waiting();
        // Answered ones leave the waiting list; if one comes back, it is a new wait.
        for (const id of [...seen.keys()]) if (!now.some((question) => question.id === id)) { seen.delete(id); left.delete(id); }
        for (const question of now) {
          if (left.has(question.id)) continue;
          if (!seen.has(question.id)) seen.set(question.id, Date.now());
          const entry = match(answers, question.text, used.get(question.id) ?? []);
          if (!entry) { left.add(question.id); log.write('operator', { event: 'teacher.no_answer', question_id: question.id, again: used.has(question.id) }); continue; }
          if (Date.now() - seen.get(question.id) < delaySeconds * 1000) continue;
          await answerOne(question, entry);
          used.set(question.id, [...(used.get(question.id) ?? []), entry.id]);
          seen.delete(question.id);
          log.write('operator', { event: 'teacher.answer', question_id: question.id, entry: entry.id });
        }
      });
    } catch (error) {
      if (!stopped) { onError(new Error(`scripted teacher failed: ${error.message}`)); return; }
    }
    if (!stopped) timer = setTimeout(poll, POLL_MS);
  }

  return {
    start() { poll(); },
    stop() { stopped = true; clearTimeout(timer); },
  };
}

module.exports = { loadAnswers, match, createTeacher };
