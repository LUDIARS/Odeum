// Microphone for the student page. The level meter runs entirely on the device and sends only a
// number (0-100) so the lecturer can see who is speaking. Speech-to-text is used only when the
// service was started with it enabled, because the browser's recogniser may send audio to the
// browser vendor; otherwise students type what they want to say.

const REPORT_MS = 200;
const QUIET_LEVEL = 12;      // matches VoiceDesk::speaking_level on the server

/**
 * @param {{ onLevel: (level: number) => void, onUtterance: (text: string) => void, onState: (text: string) => void, transcribe: boolean }} options
 */
export function createMicrophone(options) {
  let stream = null;
  let context = null;
  let timer = null;
  let recogniser = null;
  let wasQuiet = true;

  const stop = () => {
    if (timer) clearInterval(timer);
    timer = null;
    if (recogniser) { recogniser.onend = null; recogniser.stop(); recogniser = null; }
    if (stream) for (const track of stream.getTracks()) track.stop();
    stream = null;
    if (context) context.close();
    context = null;
    options.onLevel(0);
    options.onState('');
  };

  const startRecogniser = () => {
    const Recogniser = window.SpeechRecognition || window.webkitSpeechRecognition;
    if (!options.transcribe || !Recogniser) return false;
    recogniser = new Recogniser();
    recogniser.lang = 'ja-JP';
    recogniser.continuous = true;
    recogniser.interimResults = false;
    recogniser.onresult = (event) => {
      for (let i = event.resultIndex; i < event.results.length; i += 1) {
        const text = event.results[i][0].transcript.trim();
        if (event.results[i].isFinal && text) options.onUtterance(text);
      }
    };
    // The recogniser stops by itself after a pause; keep it running while the microphone is on.
    recogniser.onend = () => { if (recogniser) { try { recogniser.start(); } catch { /* already started */ } } };
    recogniser.start();
    return true;
  };

  const start = async () => {
    stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true } });
    context = new AudioContext();
    const analyser = context.createAnalyser();
    analyser.fftSize = 1024;
    context.createMediaStreamSource(stream).connect(analyser);
    const samples = new Float32Array(analyser.fftSize);
    timer = setInterval(() => {
      analyser.getFloatTimeDomainData(samples);
      let sum = 0;
      for (const sample of samples) sum += sample * sample;
      // RMS of normal speech is roughly 0.02-0.2; scale so that speech lands well above QUIET_LEVEL.
      const level = Math.min(100, Math.round(Math.sqrt(sum / samples.length) * 400));
      const quiet = level < QUIET_LEVEL;
      // While silent, one report is enough for the server to see the utterance end.
      if (!quiet || !wasQuiet) options.onLevel(level);
      wasQuiet = quiet;
    }, REPORT_MS);
    options.onState(startRecogniser() ? '話した内容が先生に届きます' : '話しているかどうかだけが先生に伝わります。内容は文字で送ってください');
  };

  return {
    get active() { return stream !== null; },
    async toggle() {
      if (stream) { stop(); return false; }
      try { await start(); return true; }
      catch { stop(); options.onState('マイクを使えませんでした。ブラウザの許可を確認してください'); return false; }
    },
    stop,
  };
}
