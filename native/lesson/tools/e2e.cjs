// Manual end-to-end check for odeum-lesson (not run by ctest). Needs Node and Playwright with Chromium.
//   ODEUM_LESSON_CONSOLE_TOKEN_FILE=/tmp/lesson.token native/build/bin/odeum-lesson &
//   NO_PROXY=127.0.0.1,localhost node native/lesson/tools/e2e.cjs /tmp
// The argument is a working directory: screenshots go to <dir>/e2e, and the token is read from
// ODEUM_LESSON_CONSOLE_TOKEN_FILE (default <dir>/console.token).
const { chromium } = require('playwright');
const fs = require('fs');
const S = process.argv[2];
fs.mkdirSync(`${S}/e2e`, { recursive: true });
(async () => {
  const token = fs.readFileSync(process.env.ODEUM_LESSON_CONSOLE_TOKEN_FILE ?? `${S}/console.token`, 'utf8').trim();
  const browser = await chromium.launch();
  const errors = [];
  const page = async (opts) => { const c = await browser.newContext(opts); const p = await c.newPage(); p.on('pageerror', e => errors.push(String(e))); p.on('console', m => { if (m.type() === 'error') errors.push(m.text()); }); return p; };
  const con = await page({ viewport: { width: 1320, height: 1000 } });
  con.on('dialog', d => d.accept('いい調子です'));
  await con.goto(`http://127.0.0.1:4411/console#token=${token}`);
  await con.click('#open');
  await con.waitForSelector('#code:not(:empty)');
  const code = (await con.textContent('#code')).trim();
  console.log('code', code, 'url', con.url());
  const join = async (name) => { const p = await page({ viewport: { width: 420, height: 900 } }); await p.goto(`http://127.0.0.1:4410/lesson#code=${code}`); await p.fill('#name', name); await p.click('#entry-form button'); await p.waitForSelector('#stage:not([hidden])'); return p; };
  const aoi = await join('あおい'), riku = await join('りく');
  await con.click('#start');
  await aoi.waitForFunction(() => document.getElementById('section-title').textContent.includes('演出が入った状態'));
  // question
  await con.selectOption('#activities', 'hitstop-design'); await con.click('#activity-open');
  await aoi.waitForSelector('#activity:not([hidden])');
  await aoi.fill('#activity-text', '威力に比例して止める長さを変える。UIは止めない'); await aoi.click('#activity-send');
  await riku.fill('#activity-text', '全部止める'); await riku.click('#activity-send');
  await con.waitForFunction(() => document.getElementById('activity-count').textContent.includes('2 / 2'));
  await con.click('#activity-reveal');
  await aoi.waitForSelector('#activity-personal:not([hidden])');
  console.log('aoi personal:', await aoi.textContent('#activity-personal'));
  console.log('riku personal:', await riku.textContent('#activity-personal'));
  // assignment + help
  await con.click('#assignment-assign');
  await aoi.waitForSelector('#assignment:not([hidden])');
  await aoi.click('#assignment-steps li:nth-child(1) input');
  await riku.click('#help');
  await riku.waitForFunction(() => document.getElementById('private').textContent.includes('ヒント'));
  console.log('riku hint:', await riku.textContent('#private li'));
  // question + share
  await aoi.fill('#question', 'ヒットストップは何秒にするのがいいですか？なぜですか'); await aoi.click('#question-form button');
  await aoi.waitForFunction(() => document.getElementById('private').textContent.includes('質問の答え'));
  await con.click('#share-best');
  await riku.waitForFunction(() => document.getElementById('shared').textContent.includes('何秒'));
  // misbehave
  for (const t of ['wwwwwwwwww', 'ああああああああ', 'うんこ']) { await riku.fill('#say', t); await riku.click('#say-form button'); await riku.waitForTimeout(150); }
  await riku.waitForSelector('#conduct[data-standing="locked"]');
  console.log('riku banner:', await riku.textContent('#conduct'));
  await con.waitForFunction(() => document.getElementById('roster').textContent.includes('発言停止'));
  await con.waitForTimeout(700);
  await aoi.screenshot({ path: `${S}/e2e/student.png`, fullPage: true });
  await riku.screenshot({ path: `${S}/e2e/student-locked.png` });
  await con.screenshot({ path: `${S}/e2e/console.png`, fullPage: true });
  // eject
  await con.click('#roster tr:nth-child(2) .ls-danger-button');
  await riku.waitForSelector('#conduct[data-standing="ejected"]');
  console.log('riku after eject:', await riku.textContent('#conduct'));
  const again = await page({});
  await again.goto(`http://127.0.0.1:4410/lesson#code=${code}`); await again.fill('#name', 'べつじん'); await again.click('#entry-form button');
  await again.waitForFunction(() => document.getElementById('entry-status').textContent.includes('締め切'));
  console.log('rejoin:', await again.textContent('#entry-status'));
  console.log('errors:', JSON.stringify(errors));
  await browser.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
