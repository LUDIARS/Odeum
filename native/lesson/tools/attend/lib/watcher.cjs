// Follows the attendee's screen: reads it a few times a second, logs every change, keeps the card
// where something happened in view (for the video), and wakes `wait --for change` callers.
const { view } = require('./seat-actions.cjs');

const POLL_MS = 300;

// Which card to bring into view for a change, most important first.
function focusFor(changed, raw) {
  if (changed.has('activity')) return '#activity';
  if (changed.has('private')) return '#private';
  if (changed.has('assignment') && raw.assignment) return '#assignment';
  if (changed.has('shared')) return '#shared';
  if (changed.has('section')) return '#narration';
  if ((changed.has('narration') || changed.has('demo')) && !(raw.activity?.state === 'open') && !raw.assignment?.active) return '#narration';
  return null;
}

function createWatcher({ page, observation, log, onError }) {
  let last = null;
  let lastParts = {};
  let version = 0;
  let latest = null;
  let timer = null;
  let stopped = false;
  const waiters = new Set();

  const parts = (raw) => ({
    lesson_state: raw.lesson_state, section: raw.section, slide: raw.slide, narration: raw.narration, demo: raw.demo,
    activity: raw.activity && { ...raw.activity, text_value: undefined }, assignment: raw.assignment,
    private: raw.private, shared: raw.shared, conduct: raw.conduct.standing,
  });

  async function poll() {
    if (stopped) return;
    try {
      const raw = await view(page);
      latest = raw;
      observation.shape(raw, { consume: false });  // records acks as soon as they appear
      const digest = observation.digest(raw);
      if (digest !== last) {
        last = digest;
        version += 1;
        const now = parts(raw);
        const changed = new Set(Object.keys(now).filter((key) => JSON.stringify(now[key]) !== JSON.stringify(lastParts[key])));
        const shown = {};
        for (const key of changed) {
          if (key === 'private') shown.private_new = raw.private.filter((item) => !(lastParts.private ?? []).some((old) => old.seq === item.seq));
          else shown[key] = now[key];
        }
        lastParts = now;
        log.write('shown', { seat: 'attendee', changed: [...changed], shown });
        const target = focusFor(changed, raw);
        if (target) await page.evaluate((selector) => document.querySelector(selector)?.scrollIntoView({ block: 'center', behavior: 'smooth' }), target);
        for (const waiter of [...waiters]) waiter(raw);
      }
    } catch (error) {
      if (!stopped) { onError(new Error(`watching the attendee seat failed: ${error.message}`)); return; }
    }
    if (!stopped) timer = setTimeout(poll, POLL_MS);
  }

  return {
    start() { poll(); },
    stop() { stopped = true; clearTimeout(timer); for (const waiter of [...waiters]) waiter(null); },
    latest: () => latest,
    version: () => version,
    // Resolves with the raw screen after the next change, or null on timeout.
    next(timeoutMs) {
      return new Promise((resolve) => {
        const done = (raw) => { clearTimeout(timeout); waiters.delete(done); resolve(raw); };
        const timeout = setTimeout(() => done(null), timeoutMs);
        waiters.add(done);
      });
    },
  };
}

module.exports = { createWatcher };
