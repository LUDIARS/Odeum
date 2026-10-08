// Program overlay rendering. Same bounds as GLab viewer-effects: telops 3 x 5s, public posts 2 x 8s,
// good particles 20 x 2s. User text is inserted with textContent only.

export const STAMP_EMOJI = { clap: '👏', laugh: '😂', wow: '😮', question: '❓', agree: '👍' };
const TELOP_LIMIT = 3, TELOP_MS = 5000;
const POST_LIMIT = 2, POST_MS = 8000;
const PARTICLE_LIMIT = 20, PARTICLE_MS = 2000;
const STAMP_MS = 2000;

function element(tag, className, text) {
  const node = document.createElement(tag);
  node.className = className;
  if (text != null) node.textContent = text;
  return node;
}

/** Keeps at most `limit` children, each removed after `lifetime` ms. */
function boundedLane(container, limit, lifetime, timers) {
  return (node) => {
    while (container.children.length >= limit) container.firstElementChild.remove();
    container.append(node);
    const timer = setTimeout(() => { node.remove(); timers.delete(timer); }, lifetime);
    timers.add(timer);
  };
}

export function createProgramOverlay(root) {
  const timers = new Set();
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const telops = element('div', 'od-telops');
  const posts = element('div', 'od-posts');
  const particles = element('div', 'od-particles');
  const stamps = element('div', 'od-stamps');
  const poll = element('div', 'od-poll');
  poll.hidden = true;
  root.append(particles, stamps, posts, telops, poll);

  const addTelop = boundedLane(telops, TELOP_LIMIT, TELOP_MS, timers);
  const addPost = boundedLane(posts, POST_LIMIT, POST_MS, timers);
  const addParticle = boundedLane(particles, PARTICLE_LIMIT, PARTICLE_MS, timers);
  const addStamp = boundedLane(stamps, 10, STAMP_MS, timers);
  let pollState = null;

  const renderPoll = (counts, answered) => {
    if (!pollState) { poll.hidden = true; return; }
    poll.replaceChildren(element('div', 'od-poll-question', pollState.question));
    const total = Math.max(1, counts.reduce((a, b) => a + b, 0));
    pollState.choices.forEach((choice, index) => {
      const row = element('div', 'od-poll-row');
      const bar = element('div', 'od-poll-bar');
      bar.style.width = `${Math.round(((counts[index] ?? 0) / total) * 100)}%`;
      row.append(element('span', 'od-poll-choice', choice), bar, element('span', 'od-poll-count', String(counts[index] ?? 0)));
      poll.append(row);
    });
    poll.append(element('div', 'od-poll-answered', `回答 ${answered}`));
    poll.hidden = false;
  };

  return {
    handle(message) {
      switch (message.type) {
        case 'telop':
          addTelop(element('div', 'od-telop', message.text));
          break;
        case 'submission': {
          if (message.show_on_screen !== true) break; // never render private text
          const card = element('div', 'od-post');
          card.append(
            element('span', 'od-post-kind', message.category === 'question' ? '質問' : '感想'),
            element('span', 'od-post-name', message.from?.name ?? ''),
            element('p', 'od-post-text', message.text),
          );
          addPost(card);
          break;
        }
        case 'reaction.burst': {
          if (!reduced) {
            const count = Math.min(message.good ?? 0, 5);
            for (let i = 0; i < count; i += 1) {
              const particle = element('span', 'od-particle', '👍');
              particle.style.left = `${10 + Math.random() * 80}%`;
              addParticle(particle);
            }
          }
          for (const [kind, n] of Object.entries(message.stamps ?? {})) {
            if (!STAMP_EMOJI[kind] || n <= 0) continue;
            addStamp(element('span', 'od-stamp', n > 1 ? `${STAMP_EMOJI[kind]}×${n}` : STAMP_EMOJI[kind]));
          }
          break;
        }
        case 'poll.open':
          pollState = { id: message.poll_id, question: message.question, choices: message.choices };
          renderPoll(message.choices.map(() => 0), 0);
          break;
        case 'tally':
          if (pollState && message.poll_id === pollState.id) renderPoll(message.counts, message.answered);
          break;
        case 'poll.closed':
          if (pollState && message.poll_id === pollState.id) {
            renderPoll(message.tally.counts, message.tally.answered);
            const closing = pollState;
            const timer = setTimeout(() => { if (pollState === closing) { pollState = null; renderPoll([], 0); } timers.delete(timer); }, 8000);
            timers.add(timer);
          }
          break;
        default:
          break;
      }
    },
    /** Called when the relay connection drops: transient items stay until they expire. */
    reset() {
      pollState = null;
      renderPoll([], 0);
    },
    dispose() {
      for (const timer of timers) clearTimeout(timer);
      timers.clear();
      root.replaceChildren();
    },
  };
}
