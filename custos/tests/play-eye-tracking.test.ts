import { describe, expect, it } from 'vitest';
import { canTrackPlay, createPlayTrackingLifecycle } from '../public/js/gaze/playTrackingLifecycle.js';
import { createPlayGazeCalibration } from '../public/js/gaze/playGazeCalibration.js';
import { createPrivateLiveTracking, trackingSample } from '../public/js/gaze/privateLiveTracking.js';

const active = { appId: 'game', role: 'player', running: true, live: true, connected: true, visible: true };
describe('private player tracking', () => {
    it('requires every play gate and never starts for spectators or free viewers', () => {
        expect(canTrackPlay(active)).toBe(true);
        for (const patch of [{ role: 'spectator' }, { role: 'free' }, { appId: null }, { running: false },
            { live: false }, { connected: false }, { visible: false }]) expect(canTrackPlay({ ...active, ...patch })).toBe(false);
    });
    it('releases on disconnect and fences callbacks from the previous app', () => {
        const owners: any[] = [], samples: any[] = [];
        const lifecycle = createPlayTrackingLifecycle({ onStatus() {}, onSample: (sample: unknown) => samples.push(sample),
            createTracker(callbacks: any) { const owner = { callbacks, stopped: 0, start: async () => {}, stop() { this.stopped++; } }; owners.push(owner); return owner; } });
        lifecycle.update(active); lifecycle.update(active);
        expect(owners).toHaveLength(1);
        lifecycle.update({ ...active, connected: false });
        owners[0].callbacks.onSample({ eyes: {} });
        expect(samples).toHaveLength(0); expect(owners[0].stopped).toBe(1);
        lifecycle.update({ ...active, appId: 'other' }); expect(owners).toHaveLength(2);
        lifecycle.destroy(); expect(owners[1].stopped).toBe(1);
    });
    it('does not repeatedly retry a denied camera on state refresh', () => {
        const owners: any[] = [];
        const lifecycle = createPlayTrackingLifecycle({ onStatus() {}, onSample() {}, createTracker(callbacks: any) {
            const owner = { callbacks, start: async () => {}, stop() {} }; owners.push(owner); return owner;
        } });
        lifecycle.update(active); owners[0].callbacks.onError(new Error('denied'));
        lifecycle.update(active); expect(owners).toHaveLength(1);
        lifecycle.retry(); expect(owners).toHaveLength(2); lifecycle.destroy();
    });
    it('stops a camera acquired after cancellation and never exposes images', async () => {
        let resolveCamera: any, stops = 0, closes = 0;
        const camera = new Promise((resolve) => { resolveCamera = resolve; });
        const track = { stop() { stops++; } };
        const tracker = createPrivateLiveTracking({ onSample() { throw new Error('late sample'); }, onError() {}, sessionClock: () => 0,
            mediaDevices: { getUserMedia: () => camera },
            loadModel: async () => ({ close() { closes++; } }),
        });
        const starting = tracker.start(); await Promise.resolve(); tracker.stop();
        resolveCamera({ getTracks: () => [track] }); await starting;
        expect(stops).toBe(1); expect(closes).toBe(1);
        expect(Object.keys(tracker).sort()).toEqual(['start', 'stop']);
        expect(trackingSample({ image: 'private', faceLandmarks: [] }, 0)).not.toHaveProperty('image');
    });
});

describe('gaze calibration', () => {
    it('requires enough eye samples instead of inventing screen coordinates', () => {
        let now = 0; const failures: string[] = [];
        const calibration = createPlayGazeCalibration({ now: () => now, onTarget() {}, onComplete() {}, onError: (message: string) => failures.push(message) });
        expect(calibration.accept({ eyes: null })).toBeNull();
        calibration.start(); now = 2100; calibration.accept({ eyes: null });
        expect(failures).toHaveLength(1); expect(calibration.isCalibrated()).toBe(false);
    });
    it('fits numeric samples, resets on geometry change and suppresses invalid gaze', () => {
        let now = 0, target: number[] | null = null;
        const calibration = createPlayGazeCalibration({ now: () => now, onTarget: (point: number[] | null) => { target = point; },
            onComplete() {}, onError(message: string) { throw new Error(message); } });
        calibration.start();
        for (let point = 0; point < 9; point++) {
            const base = now;
            for (let offset = 600; offset <= 2100; offset += 150) {
                now = base + offset;
                calibration.accept({ eyes: { irisX: target![0], irisY: target![1] }, pose: { yaw: 0, pitch: 0 } });
            }
        }
        expect(calibration.isCalibrated()).toBe(true);
        const estimate = calibration.accept({ eyes: { irisX: .5, irisY: .5 }, pose: { yaw: 0, pitch: 0 } });
        expect(estimate.x).toBeCloseTo(.5, 1); expect(estimate.y).toBeCloseTo(.5, 1);
        expect(calibration.accept({ eyes: null })).toBeNull();
        calibration.reset(); expect(calibration.isCalibrated()).toBe(false);
    });
});
