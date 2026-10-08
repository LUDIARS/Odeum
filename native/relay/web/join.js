// Guest phone page: http://<relay>/join#code=<join code>
// Same rules as the GLab reaction composer: over-length text is rejected (not truncated), input is kept
// while disconnected, questions/impressions default to private and return to private after posting.
import { STAMP_EMOJI } from './effects.js';
import { connectWithBackoff, relaySocketUrl } from './socket.js';

const TELOP_MAX = 60;
const POST_MAX = 280;
const GOOD_FLUSH_MS = 200;
const GOOD_MAX_PER_MESSAGE = 50;
const NAME_KEY = 'odeum.guest.name';

const $ = (id) => document.getElementById(id);
const scalars = (text) => [...text].length;

function storedName() {
  try { return localStorage.getItem(NAME_KEY) ?? ''; } catch { return ''; }
}
function storeName(name) {
  try { localStorage.setItem(NAME_KEY, name); } catch { /* storage unavailable */ }
}

const ERROR_TEXT = {
  session_not_live: 'コードが違うか、発表がまだ始まっていません。自動で再接続します。',
  rate_limited: '送信が多すぎます。少し待ってから送ってください。',
  invalid_name: '表示名は 1〜32 文字で入力してください。',
  reaction_unavailable: '発表者側の準備ができていません。少し待ってください。',
  capacity: '参加者が上限に達しています。',
};

let connection = null;
let textReady = false;
let pendingGood = 0;
let sentGood = 0;
let goodTimer = null;

function feedback(text) { $('feedback').textContent = text; }

function updateCounter(input, counter, max) {
  const n = scalars(input.value);
  counter.textContent = `${n} / ${max}`;
  counter.classList.toggle('od-over', n > max);
}

function flushGood() {
  goodTimer = null;
  while (pendingGood > 0) {
    const count = Math.min(pendingGood, GOOD_MAX_PER_MESSAGE);
    if (!connection?.send({ type: 'good', count })) return; // keep pending until reconnected
    pendingGood -= count;
  }
}

function sendText(message, clear) {
  if (!textReady) { feedback(ERROR_TEXT.reaction_unavailable); return; }
  if (!connection?.send(message)) { feedback('接続していません。入力はそのまま残しています。'); return; }
  clear();
  feedback('送りました');
}

function start(code, name) {
  $('entry').hidden = true;
  $('stage').hidden = false;
  connection = connectWithBackoff({
    url: () => relaySocketUrl('/v1/guest', { code, name }),
    onMessage: (message) => {
      if (message.type === 'presence') textReady = message.reaction_version === 1;
      if (message.type === 'error') feedback(ERROR_TEXT[message.code] ?? `エラー: ${message.code}`);
      if (message.type === 'welcome') flushGood();
    },
    onState: (state, detail) => {
      const text = { connecting: '接続中…', open: '参加中', waiting: '再接続を待っています' }[state];
      if (text) $('status').textContent = text;
      if (state !== 'open') textReady = false;
      if (state === 'error' && detail === 'invalid_name') {
        connection.stop();
        $('entry').hidden = false;
        $('stage').hidden = true;
      }
    },
  });
}

function mountStamps() {
  for (const [kind, emoji] of Object.entries(STAMP_EMOJI)) {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = emoji;
    button.setAttribute('aria-label', kind);
    button.addEventListener('click', () => { if (!connection?.send({ type: 'stamp', kind })) feedback('接続していません'); });
    $('stamps').append(button);
  }
}

function mountForms() {
  const telop = $('telop'), post = $('post');
  updateCounter(telop, $('telop-counter'), TELOP_MAX);
  updateCounter(post, $('post-counter'), POST_MAX);
  telop.addEventListener('input', () => updateCounter(telop, $('telop-counter'), TELOP_MAX));
  post.addEventListener('input', () => updateCounter(post, $('post-counter'), POST_MAX));

  $('good').addEventListener('click', () => {
    pendingGood += 1; sentGood += 1;
    $('good-count').textContent = String(sentGood);
    $('good').classList.remove('od-pulse'); void $('good').offsetWidth; $('good').classList.add('od-pulse');
    if (!goodTimer) goodTimer = setTimeout(flushGood, GOOD_FLUSH_MS);
  });

  $('telop-form').addEventListener('submit', (event) => {
    event.preventDefault();
    const text = telop.value.trim();
    if (!text) return;
    if (scalars(text) > TELOP_MAX) { feedback(`ツッコミは ${TELOP_MAX} 文字までです`); return; }
    sendText({ type: 'telop', text }, () => { telop.value = ''; updateCounter(telop, $('telop-counter'), TELOP_MAX); });
  });

  $('post-form').addEventListener('submit', (event) => {
    event.preventDefault();
    const text = post.value.trim();
    if (!text) return;
    if (scalars(text) > POST_MAX) { feedback(`質問・感想は ${POST_MAX} 文字までです`); return; }
    const category = new FormData($('post-form')).get('category') === 'impression' ? 'impression' : 'question';
    sendText({ type: 'submission', category, text, show_on_screen: $('show').checked === true }, () => {
      post.value = ''; $('show').checked = false; updateCounter(post, $('post-counter'), POST_MAX);
    });
  });
}

function mountEntry() {
  const fragment = new URLSearchParams(location.hash.slice(1));
  $('code').value = fragment.get('code') ?? '';
  $('name').value = storedName();
  $('entry-form').addEventListener('submit', (event) => {
    event.preventDefault();
    const code = $('code').value.trim();
    const name = $('name').value.trim();
    if (!code || !name || scalars(name) > 32) { feedback(ERROR_TEXT.invalid_name); return; }
    storeName(name);
    start(code, name);
  });
}

mountStamps();
mountForms();
mountEntry();
