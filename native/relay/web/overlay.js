// OBS browser source: http://127.0.0.1:4400/overlay#key=<overlay key>
// The key stays in the fragment so it never appears in HTTP request lines or logs.
import { createProgramOverlay } from './effects.js';
import { connectWithBackoff, relaySocketUrl } from './socket.js';

const key = new URLSearchParams(location.hash.slice(1)).get('key') ?? '';
const status = document.getElementById('status');
const overlay = createProgramOverlay(document.getElementById('overlay'));
// Status text is visible only with ?debug so a misconfigured source does not draw on air.
const debug = new URLSearchParams(location.search).has('debug');

function show(text) {
  status.textContent = text;
  status.hidden = !debug || !text;
}

if (!key) {
  show('overlay key がありません (#key=…)');
} else {
  connectWithBackoff({
    url: () => relaySocketUrl('/v1/overlay', { key }),
    onMessage: (message) => overlay.handle(message),
    onState: (state, detail) => {
      if (state === 'open') show('');
      else if (state === 'waiting') { overlay.reset(); show('発表者の接続を待っています'); }
      else if (state === 'error') show(`relay error: ${detail}`);
    },
  });
}
