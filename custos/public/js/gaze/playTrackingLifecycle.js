// Owns one camera per active player session; observation never activates it.
/** @implements SPEC-ODEUM-PLAY-GAZE */
export function canTrackPlay({ appId, role, running, live, connected, visible }) {
    return Boolean(appId) && role === 'player' && running === true && live === true
        && connected === true && visible === true;
}

/** @implements SPEC-ODEUM-PLAY-GAZE */
export function createPlayTrackingLifecycle({ createTracker, onStatus, onSample }) {
    let context = {};
    let owner = null;
    let key = null;
    let blocked = false;
    let generation = 0;
    let destroyed = false;

    function stop() {
        generation++;
        owner?.stop();
        owner = null;
        onStatus('idle', 'プレイ中だけ視線を追跡します');
    }

    function update(next) {
        context = next;
        const nextKey = canTrackPlay(next) ? next.appId : null;
        if (nextKey !== key) { stop(); blocked = false; key = nextKey; }
        if (destroyed || !key || owner || blocked) return;
        const token = ++generation;
        onStatus('starting', 'カメラと視線追跡を準備しています…');
        owner = createTracker({
            onSample: (sample) => {
                if (token !== generation) return;
                onStatus(sample.eyes ? 'tracking' : 'lost', sample.eyes ? '視線を検出中' : '目が見つかりません');
                onSample(sample);
            },
            onError: () => {
                if (token !== generation) return;
                generation++;
                owner?.stop(); owner = null; blocked = true;
                onStatus('error', '追跡できません。カメラの許可・接続と、管理者による追跡の準備を確認してください。');
            },
        });
        void owner.start();
    }

    return {
        update,
        retry() { if (destroyed) return; stop(); blocked = false; update(context); },
        destroy() { destroyed = true; key = null; stop(); },
    };
}
