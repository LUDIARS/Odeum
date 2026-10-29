// Adapted from Voluptas private tracking (9d0a05d); owned source, no sibling runtime dependency.
import { loadFaceLandmarker } from './localFaceModel.js';
import { extractGazeFeatures, headPoseProxy } from './gazeFeatures.js';

// Only numeric motion data may leave this module. Camera pixels and tracks are
// private, never attached to the DOM, a recorder, a peer connection or a request.
/** @implements SPEC-ODEUM-PLAY-GAZE */
export function trackingSample(result, sessionMs) {
  const landmarks = result?.faceLandmarks?.[0];
  const features = landmarks ? extractGazeFeatures(landmarks) : null;
  const pose = landmarks ? headPoseProxy(landmarks) : null;
  const categories = result?.faceBlendshapes?.[0]?.categories || [];
  return {
    sessionMs,
    faceDetected: Boolean(landmarks),
    eyes: features?.every(Number.isFinite)
      ? { irisX: features[0], irisY: features[1] } : null,
    pose: pose && Object.values(pose).every(Number.isFinite) ? pose : null,
    expressions: Object.fromEntries(categories
      .filter(({ categoryName, score }) => typeof categoryName === 'string'
        && Number.isFinite(score) && score >= 0 && score <= 1)
      .map(({ categoryName, score }) => [categoryName, score])),
  };
}

/** @implements SPEC-ODEUM-PLAY-GAZE */
export function createPrivateLiveTracking({ onSample, onError, sessionClock,
  mediaDevices = navigator.mediaDevices,
  createVideo = () => document.createElement('video'),
  loadModel = () => loadFaceLandmarker({ tracking: true }),
  now = () => performance.now(),
  schedule = (fn) => setTimeout(fn, 125), cancel = clearTimeout,
}) {
  let stopped = false;
  let started = false;
  let stream = null;
  let model = null;
  let video = null;
  let timer = null;
  let lastVideoTime = -1;
  let lastFrameAt = 0;

  function stop() {
    stopped = true;
    if (timer !== null) cancel(timer);
    timer = null;
    const ownedStream = stream;
    stream = null;
    ownedStream?.getTracks().forEach((track) => {
      track.removeEventListener('ended', cameraEnded);
      track.stop();
    });
    if (video) { video.pause(); video.srcObject = null; video = null; }
    const ownedModel = model;
    model = null;
    ownedModel?.close();
  }

  function fail(reason) {
    if (stopped) return;
    stop();
    onError(reason);
  }

  function cameraEnded() { fail(new Error('カメラが停止したため、目と顔の追跡を停止しました。')); }

  function tick() {
    if (stopped) return;
    try {
      if (video.readyState >= 2 && video.currentTime !== lastVideoTime) {
        lastFrameAt = now();
        lastVideoTime = video.currentTime;
        onSample(trackingSample(model.detectTracking(video, now()), sessionClock()));
      }
      if (now() - lastFrameAt > 3000) throw new Error('カメラ映像が止まったため追跡を停止しました。');
      if (!stopped) timer = schedule(tick);
    } catch (reason) { fail(reason); }
  }

  async function start() {
    if (started || stopped) return;
    started = true;
    try {
      if (!mediaDevices?.getUserMedia) throw new Error('カメラを利用できません。HTTPSで開いてください。');
      model = await loadModel();
      if (stopped) { model.close(); model = null; return; }
      stream = await mediaDevices.getUserMedia({
        video: { facingMode: 'user', width: { ideal: 320 }, height: { ideal: 240 }, frameRate: { ideal: 15 } },
        audio: false,
      });
      if (stopped) { stream.getTracks().forEach((track) => track.stop()); stream = null; return; }
      const track = stream.getVideoTracks()[0];
      if (!track || track.getSettings?.().facingMode === 'environment') {
        throw new Error('内側のカメラを利用できません。');
      }
      stream.getVideoTracks().forEach((track) => track.addEventListener('ended', cameraEnded));
      video = createVideo();
      video.muted = true;
      video.playsInline = true;
      video.srcObject = stream;
      await video.play();
      lastFrameAt = now();
      if (!stopped) tick();
    } catch (reason) { fail(reason); }
  }

  return { start, stop };
}
