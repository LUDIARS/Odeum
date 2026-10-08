// Reconnecting WebSocket for relay-served pages. The relay closes every participant when the
// presenter leaves, so pages retry with exponential backoff (1s -> 30s) until stopped.

const MIN_DELAY_MS = 1000;
const MAX_DELAY_MS = 30000;

export function relaySocketUrl(path, params) {
  const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const query = new URLSearchParams(params).toString();
  return `${scheme}//${location.host}${path}?${query}`;
}

/**
 * @param {{ url: () => string, onMessage: (message: any) => void, onState: (state: string, detail?: string) => void }} options
 */
export function connectWithBackoff(options) {
  let socket = null;
  let timer = null;
  let delay = MIN_DELAY_MS;
  let stopped = false;

  const schedule = () => {
    if (stopped) return;
    options.onState('waiting');
    timer = setTimeout(open, delay);
    delay = Math.min(delay * 2, MAX_DELAY_MS);
  };

  const open = () => {
    timer = null;
    if (stopped) return;
    options.onState('connecting');
    socket = new WebSocket(options.url());
    socket.addEventListener('message', (event) => {
      let message;
      try { message = JSON.parse(event.data); } catch { return; }
      if (message.type === 'welcome') { delay = MIN_DELAY_MS; options.onState('open'); }
      if (message.type === 'error') options.onState('error', message.code);
      options.onMessage(message);
    });
    socket.addEventListener('close', () => { socket = null; schedule(); });
  };

  open();
  return {
    send(message) {
      if (!socket || socket.readyState !== WebSocket.OPEN) return false;
      socket.send(JSON.stringify(message));
      return true;
    },
    stop() {
      stopped = true;
      if (timer) clearTimeout(timer);
      if (socket) socket.close();
    },
  };
}
