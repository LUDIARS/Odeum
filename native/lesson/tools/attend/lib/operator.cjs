// The operator's side, done with the console's real controls. Every click is followed by a check
// that the console shows the expected state; a console error or a missing control stops the run
// with the step's name.
const VERIFY_MS = 8000;
const CONNECTED = new Set(['接続中']);

async function consoleStatus(page) { return (await page.textContent('#status') ?? '').trim(); }

async function expectState(page, step, predicate, arg) {
  try {
    await page.waitForFunction(predicate, arg, { timeout: VERIFY_MS, polling: 100 });
  } catch (error) {
    const status = await consoleStatus(page);
    throw new Error(`operator step "${step}" did not take effect within ${VERIFY_MS / 1000}s (console status: "${status}")${error.name === 'TimeoutError' ? '' : `: ${error.message}`}`);
  }
  const status = await consoleStatus(page);
  if (!CONNECTED.has(status)) throw new Error(`operator step "${step}" failed: the console says "${status}"`);
}

async function click(page, step, selector) {
  const control = page.locator(selector);
  if (await control.count() !== 1) throw new Error(`operator step "${step}": control ${selector} not found on the console`);
  if (await control.isDisabled()) throw new Error(`operator step "${step}": control ${selector} is disabled`);
  await control.scrollIntoViewIfNeeded();
  await control.click();
}

async function select(page, step, selector, value) {
  const options = await page.locator(`${selector} option`).evaluateAll((nodes) => nodes.map((node) => node.value));
  if (!options.includes(value)) throw new Error(`operator step "${step}": ${selector} has no option "${value}" (has ${options.join(', ')})`);
  await page.selectOption(selector, value);
}

async function openConsole(page, { url, token, course }) {
  await page.goto(`${url}/console#token=${token}`);
  await expectState(page, 'console connect', () => document.getElementById('status').textContent.trim() === '接続中'
    && document.querySelectorAll('#plots option').length > 0);
  await select(page, 'class open', '#plots', course);
  await click(page, 'class open', '#open');
  await expectState(page, 'class open', () => document.getElementById('code').textContent.trim() !== '' && !document.getElementById('workspace').hidden);
  return (await page.textContent('#code')).replace(/[^0-9A-Z]/gi, '');
}

// The console page is driven by the schedule and by the scripted teacher; one at a time.
let consoleChain = Promise.resolve();
function exclusive(fn) { const run = consoleChain.then(fn, fn); consoleChain = run.catch(() => {}); return run; }

const activityState = (page) => page.evaluate(() => document.getElementById('activity-count').textContent.trim());

// One schedule step. Returns a short description of what the console showed afterwards.
function perform(page, step) { return exclusive(() => performNow(page, step)); }
async function performNow(page, step) {
  const name = step.label;
  switch (step.op) {
    case 'cue': {
      // 「この合図まで進める」: the console seeks to the cue and the timeline runs it itself.
      await select(page, name, '#cues', String(step.cue));
      await click(page, name, '#seek-cue');
      if (step.expect === 'activity') {
        await expectState(page, name, () => document.getElementById('activity-count').textContent.startsWith('受付中'));
        return activityState(page);
      }
      if (step.expect === 'assignment') {
        await expectState(page, name, () => document.getElementById('assignment-state').textContent.includes('実施中'));
        return (await page.textContent('#assignment-state')).trim();
      }
      if (step.expect === 'end') {
        await expectState(page, name, () => document.getElementById('player-state').textContent.trim() === '終了');
        return 'player: 終了';
      }
      await expectState(page, name, (at) => {
        const match = /^(\d+):(\d\d) \//.exec(document.getElementById('clock').textContent.trim());
        return match && Number(match[1]) * 60 + Number(match[2]) >= Math.floor(at / 1000);
      }, step.at_ms);
      return (await page.textContent('#clock')).trim();
    }
    case 'start':
      await click(page, name, '#start');
      await page.evaluate(() => window.scrollTo({ top: 0, behavior: 'smooth' }));
      await expectState(page, name, () => document.getElementById('player-state').textContent.trim() === '進行中');
      return 'player: 進行中';
    case 'skip':
      await click(page, name, '#skip');
      await page.evaluate(() => window.scrollTo({ top: 0, behavior: 'smooth' }));   // keep the progress bar in the video
      await expectState(page, name, (id) => document.querySelector('#sections button[aria-current="true"]')?.dataset.section === id, step.expect);
      return `section: ${step.expect}`;
    case 'activity.open':
      await select(page, name, '#activities', step.id);
      await click(page, name, '#activity-open');
      await expectState(page, name, () => document.getElementById('activity-count').textContent.startsWith('受付中'));
      return activityState(page);
    case 'activity.close':
      await click(page, name, '#activity-close');
      await expectState(page, name, () => document.getElementById('activity-count').textContent.startsWith('締め切り'));
      return activityState(page);
    case 'activity.reveal':
      await click(page, name, '#activity-reveal');
      await expectState(page, name, () => document.getElementById('activity-count').textContent.startsWith('結果を表示中'));
      return activityState(page);
    case 'assignment.assign':
      await select(page, name, '#assignments', step.id);
      await click(page, name, '#assignment-assign');
      await expectState(page, name, () => document.getElementById('assignment-state').textContent.includes('実施中'));
      return (await page.textContent('#assignment-state')).trim();
    case 'assignment.close':
      await click(page, name, '#assignment-close');
      await expectState(page, name, () => document.getElementById('assignment-state').textContent.includes('終了'));
      return (await page.textContent('#assignment-state')).trim();
    case 'questions.share': {
      const before = await page.locator('#questions .co-tag', { hasText: '共有済み' }).count();
      await click(page, name, '#share-best');
      await page.waitForTimeout(1200);
      const status = await consoleStatus(page);
      if (!CONNECTED.has(status)) throw new Error(`operator step "${name}" failed: the console says "${status}"`);
      const after = await page.locator('#questions .co-tag', { hasText: '共有済み' }).count();
      return `shared ${after - before} question(s) (${after} shared in total)`;
    }
    default:
      throw new Error(`unknown operator step ${step.op}`);
  }
}

// Lesson position as the console shows it ("m:ss / m:ss"), in seconds.
async function clock(page) {
  const text = (await page.textContent('#clock') ?? '').trim();
  const match = /^(\d+):(\d\d) \/ (\d+):(\d\d)$/.exec(text);
  if (!match) return null;
  return { position: Number(match[1]) * 60 + Number(match[2]), duration: Number(match[3]) * 60 + Number(match[4]) };
}

module.exports = { openConsole, perform, clock, exclusive };
