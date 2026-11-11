#!/usr/bin/env node
// Session runner: starts odeum-lesson, opens the class on a recorded operator console, seats the
// attendee (recorded) and scripted classmates, drives the course on a condensed schedule with the
// console's controls, serves the attendee's control API (seat.cjs), and on stop composes the video.
//
//   NO_PROXY=127.0.0.1,localhost node native/lesson/tools/attend/classroom.cjs --work <dir> [--students agent,りく,はな]
// See README.md in this folder for the options.
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');
const { parseArgs, numberOption } = require('./lib/args.cjs');
const { startService, loadTimeline, DEFAULT_BIN } = require('./lib/service.cjs');
const { buildSchedule, estimateSeconds, activitiesOf, sectionsOf, describeLimits, describeWindows, DEFAULTS } = require('./lib/schedule.cjs');
const { loadAnswers, createTeacher } = require('./lib/teacher.cjs');
const { openConsole } = require('./lib/operator.cjs');
const { createRunner } = require('./lib/runner.cjs');
const { installHooks } = require('./lib/seat-dom.cjs');
const actions = require('./lib/seat-actions.cjs');
const { createObservation } = require('./lib/observation.cjs');
const { createWatcher } = require('./lib/watcher.cjs');
const { createClassmate } = require('./lib/classmates.cjs');
const { createControlServer, MAX_WAIT_SECONDS } = require('./lib/control-server.cjs');
const { createEventLog } = require('./lib/event-log.cjs');
const video = require('./lib/video.cjs');

const SEAT_VIEW = { width: 480, height: 900 };
const CONSOLE_VIEW = { width: 1360, height: 900 };   // the console page is up to 1320 px wide, plus the scrollbar

function options() {
  const { options: o } = parseArgs(process.argv.slice(2), {
    flags: ['no-wait-attendee', 'help'],
    allow: ['work', 'course', 'students', 'bin', 'plot', 'activity-seconds', 'choice-seconds', 'text-seconds', 'windows', 'assignment-seconds', 'narration-seconds',
      'section-seconds', 'demo-seconds', 'reveal-seconds', 'share-after', 'share-hold', 'linger', 'attendee-timeout', 'teacher-answers', 'teacher-delay'],
  });
  if (o.help) {
    console.log(fs.readFileSync(path.join(__dirname, 'README.md'), 'utf8'));
    // The longest windows this course's own timeline allows (the lesson closes each one by itself after that).
    try {
      const timeline = loadTimeline({ bin: o.bin ? path.resolve(o.bin) : DEFAULT_BIN, course: o.course ?? 'action-game', plot: o.plot });
      const schedule = buildSchedule(timeline);
      console.log(`Window limits for ${o.plot ?? o.course ?? 'action-game'} (the lesson's own time minus ${DEFAULTS.marginSeconds}s):\n${describeLimits(schedule.limits)}`);
      console.log(`Default windows (choice ${DEFAULTS.choiceSeconds}s, text / missing / open ${DEFAULTS.textSeconds}s), about ${Math.round(estimateSeconds(schedule) / 60)} min in all:\n${describeWindows(schedule)}`);
    } catch (error) { console.log(`(window limits unavailable: ${error.message})`); }
    process.exit(0);
  }
  const work = o.work ?? process.env.ATTEND_WORK;
  if (!work) throw new Error('--work <dir> (or ATTEND_WORK) is required');
  const students = (o.students ?? 'agent,りく,はな').split(',').map((name) => name.trim()).filter(Boolean);
  if (students.length === 0) throw new Error('--students needs at least the attendee');
  if (new Set(students).size !== students.length) throw new Error('--students names must be distinct');
  const windows = {};
  for (const pair of (o.windows ?? '').split(',').filter(Boolean)) {
    const [id, seconds] = pair.split('=');
    if (!id || !(Number(seconds) > 0)) throw new Error(`--windows expects id=seconds[,id=seconds] (got ${pair})`);
    windows[id] = Number(seconds);
  }
  return {
    work: path.resolve(work), course: o.course ?? 'action-game', students, bin: o.bin ? path.resolve(o.bin) : DEFAULT_BIN, plot: o.plot,
    windows, linger: numberOption(o, 'linger', 20),
    teacherAnswers: o['teacher-answers'] ? path.resolve(o['teacher-answers']) : null, teacherDelay: numberOption(o, 'teacher-delay', 12), waitAttendee: !o['no-wait-attendee'], attendeeTimeout: numberOption(o, 'attendee-timeout', 600, { min: 1 }),
    // Undefined = the schedule's default (clamped to the lesson's limit if need be); a value given is used as is or refused.
    settings: {
      choiceSeconds: numberOption(o, 'choice-seconds', numberOption(o, 'activity-seconds', undefined, { min: 5 }), { min: 5 }),
      textSeconds: numberOption(o, 'text-seconds', numberOption(o, 'activity-seconds', undefined, { min: 5 }), { min: 5 }),
      assignmentSeconds: numberOption(o, 'assignment-seconds', DEFAULTS.assignmentSeconds, { min: 10 }),
      narrationSeconds: numberOption(o, 'narration-seconds', 8, { min: 2 }),
      sectionSeconds: numberOption(o, 'section-seconds', 15, { min: 2 }),
      demoSeconds: numberOption(o, 'demo-seconds', 8, { min: 2 }),
      revealSeconds: numberOption(o, 'reveal-seconds', 12, { min: 3 }),
      shareAfterSeconds: numberOption(o, 'share-after', 20, { min: 2 }),
      shareHoldSeconds: numberOption(o, 'share-hold', 20, { min: 2 }),
    },
  };
}

const cleanup = [];   // undone in reverse if start-up fails before the session is running

async function main() {
  const opt = options();
  fs.mkdirSync(opt.work, { recursive: true, mode: 0o700 });
  const sessionFile = path.join(opt.work, 'session.json');
  const socketPath = path.join(opt.work, 'seat.sock');
  if (fs.existsSync(sessionFile)) {
    const previous = JSON.parse(fs.readFileSync(sessionFile, 'utf8'));
    let alive = false;
    try { process.kill(previous.pid, 0); alive = previous.state !== 'finished'; } catch { alive = false; }
    if (alive) throw new Error(`a session (pid ${previous.pid}) is already running in ${opt.work}`);
  }
  const writeSession = (extra) => fs.writeFileSync(sessionFile, JSON.stringify({ pid: process.pid, socket: socketPath, course: opt.course, ...extra }, null, 2));

  const timeline = loadTimeline({ bin: opt.bin, course: opt.course, plot: opt.plot });
  const schedule = buildSchedule(timeline, opt.settings, opt.windows);
  const activities = activitiesOf(timeline);
  for (const id of Object.keys(opt.windows)) if (![...activities.values()].some((plan) => plan.id === id)) throw new Error(`--windows: the course has no activity "${id}"`);
  const sectionIdOf = (title) => sectionsOf(timeline).find((section) => section.title === title)?.id ?? null;
  const estimate = estimateSeconds(schedule);
  const answers = opt.teacherAnswers ? loadAnswers(opt.teacherAnswers) : null;
  for (const entry of answers ?? []) if (entry.after && ![...activities.values()].some((plan) => plan.id === entry.after))
    throw new Error(`--teacher-answers: entry "${entry.id}" waits for "${entry.after}", which the course does not have`);

  const log = createEventLog(path.join(opt.work, 'attendee.jsonl'));
  const videoDir = path.join(opt.work, 'video');
  fs.rmSync(videoDir, { recursive: true, force: true });
  log.write('session', { event: 'start', course: opt.course, students: opt.students, settings: schedule.settings, windows: opt.windows, limits: schedule.limits,
    windows_used: schedule.windows, clamped: schedule.clamped,
    teacher: answers ? { file: opt.teacherAnswers, entries: answers.map((entry) => entry.id), delay_seconds: opt.teacherDelay } : null, estimate_seconds: estimate });
  console.log(`[classroom] course ${opt.course}: ${schedule.steps.length} schedule steps, about ${Math.round(estimate / 60)} min`);

  const service = await startService({ bin: opt.bin, work: opt.work, plot: opt.plot });
  cleanup.push(() => service.stop());
  const browser = await chromium.launch();
  cleanup.push(() => browser.close());
  const errors = [];
  const contexts = [];
  const newSeat = async (viewport, record) => {
    const context = await browser.newContext({ viewport, ...(record ? { recordVideo: { dir: path.join(videoDir, record), size: viewport } } : {}) });
    contexts.push(context);
    const startedAt = Date.now();
    const page = await context.newPage();
    page.on('pageerror', (error) => errors.push(`${record ?? 'classmate'}: ${error}`));
    return { context, page, startedAt };
  };

  let shuttingDown = null;
  let exitCode = 0;
  let control = null;
  let watcher = null;
  let runner = null;
  let teacher = null;
  let attendeeStarted = false;
  const classmates = [];
  const fatal = (error) => { console.error(`[classroom] FAIL: ${error.message}`); log.write('error', { where: 'session', message: error.message }); exitCode = 1; shutdown(); };

  // Both recorded pages are created first, back to back, so the views start almost together.
  const operator = await newSeat(CONSOLE_VIEW, 'console');
  const attendee = await newSeat(SEAT_VIEW, 'attendee');
  log.setOrigin(Math.min(operator.startedAt, attendee.startedAt));
  await attendee.page.addInitScript(installHooks);

  const code = await openConsole(operator.page, { url: service.consoleUrl, token: service.token, course: opt.course });
  log.write('operator', { event: 'class.open', course: opt.course });
  const join = async (seat, name) => {
    await seat.page.goto(`${service.studentUrl}/lesson#code=${code}`);
    await seat.page.fill('#name', name);
    await seat.page.click('#entry-form button[type=submit]');
    try { await seat.page.waitForSelector('#stage:not([hidden])', { timeout: 10000 }); } catch {
      throw new Error(`seat ${name} could not join: "${(await seat.page.textContent('#entry-status')).trim()}"`);
    }
  };
  await join(attendee, opt.students[0]);
  const observation = createObservation(activities);
  for (const [slot, name] of opt.students.slice(1).entries()) {
    const seat = await newSeat({ width: 420, height: 860 }, null);
    await seat.page.addInitScript(installHooks);
    await join(seat, name);
    classmates.push(createClassmate({ name, page: seat.page, slot, observation, log, onError: fatal }));
  }
  log.write('session', { event: 'seated', attendee: opt.students[0], classmates: opt.students.slice(1) });

  watcher = createWatcher({ page: attendee.page, observation, log, onError: fatal });
  watcher.start();
  runner = createRunner({ page: operator.page, schedule, log, seat: () => watcher.latest() });
  if (answers) teacher = createTeacher({ page: operator.page, answers, delaySeconds: opt.teacherDelay, revealed: () => runner.revealed(), log, onError: fatal });

  // ---- control API ----
  let attendeeArrived = null;
  const arrived = new Promise((resolve) => { attendeeArrived = resolve; });
  let returnedVersion = 0;
  // Only observe / wait hand out private messages as new (and mark them read). An action's reply shows
  // what is new too, but leaves it unread, so the next observe / wait still returns it exactly once.
  const look = async ({ all = false, consume = true } = {}) => {
    const raw = await actions.view(attendee.page);
    if (consume) returnedVersion = watcher.version();
    return { ...observation.shape(raw, { all, consume }), video_seconds: log.seconds() };
  };
  const act = (name, fn) => ({
    serial: true,
    run: async (input) => {
      attendeeArrived();
      const before = await actions.view(attendee.page);
      const plan = before.activity ? observation.activityId(before.activity.prompt) : null;
      const result = await fn(input, plan);
      // A link code is a credential: it goes back to the attendee but is not written to the log.
      const logged = result.link ? { ...result, link: '(not logged)' } : result;
      log.write('action', { seat: 'attendee', action: name, input, ok: result.ok, result: logged });
      if (name === 'answer' && plan) {
        observation.recordAnswer(plan.id, input.text ?? input.choices);
        if (result.acked) observation.recordAck(plan.id);
      }
      return { ...result, action: name, observe: await look({ consume: false }) };
    },
  });
  const handlers = {
    observe: { run: async (input) => { attendeeArrived(); return { ok: true, ...await look(input) }; } },
    wait: {
      run: async (input) => {
        attendeeArrived();
        const seconds = Math.min(MAX_WAIT_SECONDS, Math.max(0, Number(input.timeout ?? 30)));
        if (input.for && input.for !== 'change') return { ok: false, error: { source: 'harness', code: 'bad_wait', text: 'only --for change is supported' } };
        const changed = watcher.version() > returnedVersion || (await watcher.next(seconds * 1000)) !== null;
        return { ok: true, changed, ...await look(input) };
      },
    },
    answer: act('answer', (input) => actions.answer(attendee.page, input)),
    tick: act('tick', (input) => actions.tick(attendee.page, input)),
    help: act('help', () => actions.help(attendee.page)),
    ask: act('ask', (input) => actions.ask(attendee.page, input)),
    say: act('say', (input) => actions.say(attendee.page, input)),
    vote: act('vote', (input) => actions.vote(attendee.page, input)),
    'tool-link': act('tool-link', () => actions.toolLink(attendee.page)),
    unresolved: act('unresolved', (input) => actions.unresolved(attendee.page, input)),
    status: {
      run: async () => ({
        ok: true, schedule: runner.status(), estimate_seconds: Math.round(estimate), video_seconds: log.seconds(),
        lesson_clock: (await operator.page.textContent('#clock'))?.trim() ?? null, waiting_for_attendee: !attendeeStarted,
      }),
    },
    stop: { run: async () => { setTimeout(shutdown, 50); return { ok: true, stopping: true }; } },
  };
  control = createControlServer({ socketPath, handlers });
  await control.listen();
  writeSession({ state: 'ready', started: new Date().toISOString(), work: opt.work });
  cleanup.length = 0;   // from here on, shutdown() owns the teardown

  async function shutdown() {
    if (shuttingDown) return shuttingDown;
    shuttingDown = (async () => {
      console.log('[classroom] stopping: closing pages so the videos are finalised');
      runner?.stop();
      teacher?.stop();
      for (const classmate of classmates) classmate.stop();
      watcher?.stop();
      await control?.close();
      const views = [];
      for (const seat of [attendee, operator]) {
        const recorded = seat.page.video();
        await seat.context.close();
        views.push({ file: await recorded.path(), startedAt: seat.startedAt, ...(seat === attendee ? SEAT_VIEW : CONSOLE_VIEW) });
      }
      for (const context of contexts) await context.close().catch(() => {});
      await browser.close();
      await service.stop();
      log.write('session', { event: 'stop', page_errors: errors });
      const result = { state: 'finished', exit_code: exitCode, log: path.join(opt.work, 'attendee.jsonl'), raw_videos: views.map((view) => view.file) };
      try {
        const output = path.join(opt.work, 'session.mp4');
        video.compose({ views, output });
        result.video = video.verify(output, path.join(opt.work, 'frames'));
        result.raw = views.map((view) => video.probe(view.file));
      } catch (error) {
        result.video_error = error.message;
        exitCode = 1;
        result.exit_code = 1;
      }
      log.close();
      writeSession(result);
      console.log(JSON.stringify(result, null, 2));
      if (errors.length) console.error(`[classroom] page errors: ${JSON.stringify(errors)}`);
      process.exit(exitCode);
    })();
    return shuttingDown;
  }
  process.on('SIGINT', () => { console.log('[classroom] SIGINT'); shutdown(); });
  process.on('SIGTERM', () => shutdown());

  console.log(`[classroom] ready. attendee seat "${opt.students[0]}", classmates: ${opt.students.slice(1).join(', ') || 'none'}`);
  console.log(`[classroom] attend with: ATTEND_WORK=${opt.work} node ${path.join(__dirname, 'seat.cjs')} observe`);
  if (opt.waitAttendee) {
    console.log('[classroom] waiting for the attendee\'s first seat command before starting the lesson');
    const timeout = new Promise((resolve) => setTimeout(() => resolve('timeout'), opt.attendeeTimeout * 1000));
    if (await Promise.race([arrived, timeout]) === 'timeout') { fatal(new Error(`the attendee did not call seat.cjs within ${opt.attendeeTimeout}s`)); return; }
  }
  attendeeStarted = true;
  writeSession({ state: 'running', started: new Date().toISOString(), work: opt.work });
  console.log('[classroom] lesson starting');
  for (const classmate of classmates) classmate.start(sectionIdOf);
  teacher?.start();
  runner.start().then(async () => {
    if (runner.state() !== 'done') return;
    console.log(`[classroom] schedule finished; stopping in ${opt.linger}s${opt.linger === 0 ? ' (0 = wait for stop)' : ''}`);
    if (opt.linger > 0) setTimeout(shutdown, opt.linger * 1000);
  }, fatal);
}

main().catch(async (error) => {
  console.error(`[classroom] FAIL: ${error.message}`);
  for (const undo of cleanup.reverse()) await undo().catch(() => {});
  process.exit(1);
});
