import { fitAffineMapping, applyMapping } from './gazeCalibration.js';

const TARGETS = [[.15,.15],[.5,.15],[.85,.15],[.15,.5],[.5,.5],[.85,.5],[.15,.85],[.5,.85],[.85,.85]];

// Bounded numeric-only calibration. Each point contributes at most 20 samples.
/** @implements SPEC-ODEUM-PLAY-GAZE */
export function createPlayGazeCalibration({ now, onTarget, onComplete, onError }) {
    let index = -1;
    let since = 0;
    let samples = [];
    let count = 0;
    let mapping = null;
    function reset() { index = -1; samples = []; count = 0; mapping = null; onTarget(null); }
    function start() { reset(); index = 0; since = now(); onTarget(TARGETS[0]); }
    function accept(sample) {
        const features = sample.eyes && sample.pose
            ? [sample.eyes.irisX, sample.eyes.irisY, sample.pose.yaw, sample.pose.pitch, 1] : null;
        if (index >= 0) {
            const elapsed = now() - since;
            if (features && features.every(Number.isFinite) && elapsed >= 600 && elapsed < 2000 && count < 20) {
                const [x, y] = TARGETS[index];
                samples.push({ features, x, y }); count++;
            }
            if (elapsed >= 2000) {
                if (count < 4) { reset(); onError('目を読み取れませんでした。明るさと顔の位置を確認して、やり直してください。'); return null; }
                index++; count = 0; since = now();
                if (index === TARGETS.length) {
                    const fitted = fitAffineMapping(samples);
                    reset();
                    if (!fitted || !Number.isFinite(fitted.fitError) || fitted.fitError > .2) {
                        onError('視線の位置を合わせられませんでした。端末と顔を動かさず、やり直してください。');
                    } else { mapping = fitted; onComplete(); }
                } else onTarget(TARGETS[index]);
            }
            return null;
        }
        if (!mapping || !features || !features.every(Number.isFinite)) return null;
        const point = applyMapping(mapping, features);
        return Number.isFinite(point.x) && Number.isFinite(point.y)
            && point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1 ? point : null;
    }
    return { start, reset, accept, isCalibrating: () => index >= 0, isCalibrated: () => mapping !== null };
}
