import { createPrivateLiveTracking } from './gaze/privateLiveTracking.js';
import { createPlayTrackingLifecycle } from './gaze/playTrackingLifecycle.js';
import { createPlayGazeCalibration } from './gaze/playGazeCalibration.js';

/**
 * Odeum-only adaptation: camera pixels never enter game media, snapshots or WS.
 * @implements SPEC-ODEUM-PLAY-GAZE
 */
export function createPlayEyeTracking({ frame, getMedia, status, retry, calibrate, cancel, onCalibrationStart }) {
    const target = document.createElement('div');
    target.className = 'gaze-calibration'; target.hidden = true;
    const spot = document.createElement('span'); spot.className = 'gaze-target';
    const instruction = document.createElement('span');
    instruction.textContent = '点を見てください（ゲームへの操作は一時停止）';
    target.append(spot, instruction); frame.append(target);
    const dot = document.createElement('span'); dot.className = 'gaze-point'; dot.hidden = true;
    dot.setAttribute('aria-hidden', 'true'); frame.append(dot);
    let state = 'idle';
    let destroyed = false;
    let lastContext = {};
    let calibrationMessage = '';
    let geometry = '';

    function place(element, point) {
        const media = getMedia();
        if (!media) return;
        const box = media.getBoundingClientRect(), base = frame.getBoundingClientRect();
        const width = media.videoWidth || media.naturalWidth;
        const height = media.videoHeight || media.naturalHeight;
        if (!width || !height) return;
        const scale = Math.min(box.width / width, box.height / height);
        element.style.left = `${box.left - base.left + (box.width - width * scale) / 2 + point.x * width * scale}px`;
        element.style.top = `${box.top - base.top + (box.height - height * scale) / 2 + point.y * height * scale}px`;
    }
    const calibration = createPlayGazeCalibration({
        now: () => performance.now(),
        onTarget(point) { target.hidden = !point; cancel.hidden = !point; if (point) place(spot, { x: point[0], y: point[1] }); },
        onComplete() { calibrationMessage = '位置合わせ完了（推定位置）'; },
        onError(message) { calibrationMessage = message; status.textContent = message; },
    });
    const lifecycle = createPlayTrackingLifecycle({
        createTracker: (callbacks) => createPrivateLiveTracking({ ...callbacks, sessionClock: () => performance.now() }),
        onStatus(next, message) {
            state = next;
            retry.hidden = next !== 'error';
            calibrate.disabled = next !== 'tracking' && next !== 'lost';
            if (next === 'idle' || next === 'error' || next === 'starting') {
                calibration.reset(); dot.hidden = true; calibrationMessage = '';
            }
            status.textContent = `${message}${calibrationMessage ? ` — ${calibrationMessage}` : ''}`;
        },
        onSample(sample) {
            const rect = getMedia()?.getBoundingClientRect();
            const nextGeometry = rect ? [rect.left, rect.top, rect.width, rect.height].map(Math.round).join(':') : '';
            if (geometry && geometry !== nextGeometry) resetCalibration();
            geometry = nextGeometry;
            const point = calibration.accept(sample);
            dot.hidden = !point;
            if (point) place(dot, point);
            calibrate.disabled = calibration.isCalibrating() || !sample.eyes;
            if (!calibration.isCalibrated() && !calibration.isCalibrating() && !calibrationMessage) {
                status.textContent += ' — 「視線の位置を合わせる」を押してください';
            }
        },
    });
    function update(context) { lastContext = context; if (!destroyed) lifecycle.update({ ...context, visible: !document.hidden }); }
    function visibility() { update(lastContext); }
    function pagehide() { lifecycle.update({ ...lastContext, visible: false }); }
    function resetCalibration() { calibration.reset(); dot.hidden = true; calibrationMessage = '画面が変わったため位置合わせをやり直してください'; }
    const observer = new ResizeObserver(resetCalibration); observer.observe(frame);
    function startCalibration() { if (state === 'tracking' || state === 'lost') { onCalibrationStart(); calibrationMessage = ''; calibration.start(); dot.hidden = true; } }
    function retryTracking() { lifecycle.retry(); }
    function cancelCalibration() { calibration.reset(); calibrationMessage = '位置合わせを中止しました'; }
    function blockInput(event) { if (calibration.isCalibrating()) { event.preventDefault(); event.stopPropagation(); } }
    const blockedEvents = ['pointerdown', 'pointerup', 'pointermove', 'keydown', 'keyup'];
    blockedEvents.forEach((event) => frame.addEventListener(event, blockInput, true));
    calibrate.addEventListener('click', startCalibration); retry.addEventListener('click', retryTracking);
    cancel.addEventListener('click', cancelCalibration);
    document.addEventListener('visibilitychange', visibility);
    window.addEventListener('pagehide', pagehide);
    window.addEventListener('pageshow', visibility);
    function destroy() {
        if (destroyed) return; destroyed = true;
        lifecycle.destroy(); calibration.reset(); observer.disconnect();
        calibrate.removeEventListener('click', startCalibration); retry.removeEventListener('click', retryTracking);
        cancel.removeEventListener('click', cancelCalibration);
        document.removeEventListener('visibilitychange', visibility); window.removeEventListener('pagehide', pagehide);
        window.removeEventListener('pageshow', visibility);
        blockedEvents.forEach((event) => frame.removeEventListener(event, blockInput, true));
        target.remove(); dot.remove();
    }
    return { update, destroy, isCalibrating: calibration.isCalibrating };
}
