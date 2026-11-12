/**
 * Records golden JSON from the former Node runner (custos/src) for the C++ compatibility tests.
 *
 *   cd custos && npm ci --ignore-scripts
 *   npx tsx ../native/runner/tests/golden/generate.mts
 *
 * CUSTOS_SRC (default: <repo>/custos/src) and GOLDEN_OUT (default: <repo>/native/tests/fixtures/runner)
 * override the locations. Only pure functions and in-memory classes are exercised: no process, no
 * network, no file outside GOLDEN_OUT. Clocks and random sources are fixed so the output is stable.
 */
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, "..", "..", "..", "..");
const src = resolve(process.env.CUSTOS_SRC ?? join(repo, "custos", "src"));
const out = resolve(process.env.GOLDEN_OUT ?? join(repo, "native", "tests", "fixtures", "runner"));
process.env.CUSTOS_VIDEO_ENCODER = "x264"; // no ffmpeg probe while recording
process.env.LOG_LEVEL = "silent";

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Any = any;
const load = async (path: string): Promise<Any> => import(pathToFileURL(join(src, path)).href);
const write = (name: string, value: unknown) => {
    mkdirSync(out, { recursive: true });
    writeFileSync(join(out, name), JSON.stringify(value, null, 2) + "\n", "utf8");
};
const attempt = (f: () => unknown) => {
    try { return { ok: true, value: f() }; } catch { return { ok: false }; }
};

const config = await load("config/apps-config.ts");
const appsRoutes = await load("routes/apps-routes.ts");
const comment = await load("ws/comment-message.ts");
const pointer = await load("ws/pointer-message.ts");
const room = await load("audience/room.ts");
const handler = await load("ws/handler.ts");
const prefs = await load("runtime/stream-prefs.ts");
const lease = await load("input/control-lease.ts");

// ── apps.json ────────────────────────────────────────────────────────────────
const app = (extra: Record<string, unknown> = {}) => ({ id: "demo", name: "Demo", run: { cwd: ".", cmd: "x" }, ...extra });
const configInputs: Record<string, unknown> = {
    "minimal": { apps: [app()] },
    "empty apps": { apps: [] },
    "version 1": { version: 1, apps: [] },
    "version 2 rejected": { version: 2, apps: [] },
    "missing apps": {},
    "apps not array": { apps: {} },
    "run with args env timeout": { apps: [app({ run: { cwd: ".", cmd: "echo", args: ["hi", "there"], env: { FOO: "bar", A: "b" }, timeoutSec: 60 } })] },
    "workingDir only": { apps: [app({ run: { workingDir: "/tmp/work", cmd: "x" } })] },
    "workingDir wins": { apps: [app({ run: { workingDir: "/new", cwd: "/old", cmd: "x" } })] },
    "missing working dir": { apps: [app({ run: { cmd: "x" } })] },
    "empty cwd": { apps: [app({ run: { cwd: "", cmd: "x" } })] },
    "empty cmd": { apps: [app({ run: { cwd: ".", cmd: "" } })] },
    "args not strings": { apps: [app({ run: { cwd: ".", cmd: "x", args: [1] } })] },
    "env not strings": { apps: [app({ run: { cwd: ".", cmd: "x", env: { A: 1 } } })] },
    "timeout negative": { apps: [app({ run: { cwd: ".", cmd: "x", timeoutSec: -1 } })] },
    "timeout fraction": { apps: [app({ run: { cwd: ".", cmd: "x", timeoutSec: 1.5 } })] },
    "timeout exponent": { apps: [app({ run: { cwd: ".", cmd: "x", timeoutSec: 1e2 } })] },
    "build and test": { apps: [app({ build: { workingDir: "/b", cmd: "cmake", args: ["--build", "."] }, test: { workingDir: "/t", cmd: "ctest" } })] },
    "build null": { apps: [app({ build: null })] },
    "bad id": { apps: [{ id: "Bad ID with spaces", name: "x", run: { cwd: ".", cmd: "x" } }] },
    "upper id": { apps: [app({ id: "Demo-1" })] },
    "leading hyphen id": { apps: [app({ id: "-demo" })] },
    "duplicate ids": { apps: [app(), app()] },
    "empty name": { apps: [app({ name: "" })] },
    "description": { apps: [app({ description: "text" })] },
    "android target": { apps: [app({ target: "android" })] },
    "bad target": { apps: [app({ target: "ios" })] },
    "unknown keys dropped": { apps: [app({ extra: 1, run: { cwd: ".", cmd: "x", extra: true } })], extra: "x" },
    "capture defaults": { apps: [app({ capture: { type: "window" } })] },
    "capture full": { apps: [app({ capture: { type: "window", windowTitle: "W", androidSerial: "S", fps: 60, preset: "fast", intervalSec: 0.5 } })] },
    "capture fps 0": { apps: [app({ capture: { type: "window", fps: 0 } })] },
    "capture fps 61": { apps: [app({ capture: { type: "window", fps: 61 } })] },
    "capture interval 60": { apps: [app({ capture: { type: "fullscreen", intervalSec: 60 } })] },
    "capture interval 61": { apps: [app({ capture: { type: "fullscreen", intervalSec: 61 } })] },
    "capture bad type": { apps: [app({ capture: { type: "screen" } })] },
    "button key": { apps: [app({ input: { buttons: [{ label: "Jump", key: "Space" }] } })] },
    "button action": { apps: [app({ input: { buttons: [{ label: "Quit", action: "kill" }] } })] },
    "button neither": { apps: [app({ input: { buttons: [{ label: "broken" }] } })] },
    "button both": { apps: [app({ input: { buttons: [{ label: "both", key: "W", action: "kill" }] } })] },
    "button empty key with action": { apps: [app({ input: { buttons: [{ label: "x", key: "", action: "noop" }] } })] },
    "button empty key alone": { apps: [app({ input: { buttons: [{ label: "x", key: "" }] } })] },
    "button bad action": { apps: [app({ input: { buttons: [{ label: "x", action: "explode" }] } })] },
    "input flags": { apps: [app({ input: { allowKeyboard: false, allowMouse: false } })] },
    "pad": { apps: [app({ input: { pad: {
        leftStick: { up: "W", down: "S", left: "A", right: "D", press: "Q" },
        rightStick: { up: "Up", down: "Down", left: "Left", right: "Right" },
        dpad: { up: "I", down: "K", left: "J", right: "L" },
        face: { a: "Space", x: "E" }, shoulders: { lb: "Q", rt: "R" },
        menu: [{ label: "Shot", action: "screenshot" }, { label: "Menu", key: "Escape" }],
    } } })] },
    "pad empty": { apps: [app({ input: { pad: {} } })] },
    "pad bad direction": { apps: [app({ input: { pad: { dpad: { up: "", down: "S", left: "A", right: "D" } } } })] },
    "logs": { apps: [app({ logs: { stdout: false, files: ["a.log"] } })] },
    "bridge ergo default": { apps: [app({ inAppBridge: { kind: "ergo" } })] },
    "bridge unity default": { apps: [app({ inAppBridge: { kind: "unity" } })] },
    "bridge explicit port": { apps: [app({ inAppBridge: { kind: "unity", port: 8888, host: "localhost", fps: 30 } })] },
    "bridge kind omitted": { apps: [app({ inAppBridge: { host: "127.0.0.1", port: 5198 } })] },
    "bridge bad kind": { apps: [app({ inAppBridge: { kind: "unknown" } })] },
    "bridge bad port": { apps: [app({ inAppBridge: { kind: "unity", port: 99999 } })] },
    "bridge fps 121": { apps: [app({ inAppBridge: { fps: 121 } })] },
    "bridge video override": { apps: [app({ inAppBridge: { kind: "unity", video: { codec: "rawvideo", width: 1920, height: 1080, bitrate: 5000000, maxBitrate: 8000000 } } })] },
    "bridge video bitrate above max": { apps: [app({ inAppBridge: { kind: "unity", video: { bitrate: 9000000 } } })] },
    "bridge video width 63": { apps: [app({ inAppBridge: { video: { width: 63 } } })] },
    "ergoCustos legacy": { apps: [app({ ergoCustos: { host: "127.0.0.1", port: 5198 } })] },
    "ergoCustos empty": { apps: [app({ ergoCustos: {} })] },
    "ergoCustos and inAppBridge": { apps: [app({ ergoCustos: { port: 5198 }, inAppBridge: { kind: "unity", port: 5199 } })] },
    "ergoCustos bad port": { apps: [app({ ergoCustos: { port: 0 } })] },
    "playLog defaults": { apps: [app({ playLog: {} })] },
    "playLog values": { apps: [app({ playLog: { enabled: false, statePollMs: 100, idleStopSec: 86400 } })] },
    "playLog poll 99": { apps: [app({ playLog: { statePollMs: 99 } })] },
    "audio": { apps: [app({ audio: { source: "process-loopback" } })] },
    "audio bad source": { apps: [app({ audio: { source: "mic" } })] },
};
write("config_cases.json", Object.entries(configInputs).map(([name, input]) => {
    const parsed = config.appsRootSchema.safeParse(input);
    let output: unknown = null;
    let ok = parsed.success;
    if (ok) {
        const ids = new Set<string>();
        for (const a of parsed.data.apps) { if (ids.has(a.id)) ok = false; ids.add(a.id); }
        output = ok ? JSON.parse(JSON.stringify(parsed.data)) : null;
    }
    return { name, input, ok, output };
}));

// ── /api/apps redact ─────────────────────────────────────────────────────────
write("redact_cases.json", ["minimal", "capture full", "bridge unity default", "pad", "audio", "playLog values", "description", "build and test"]
    .map((name) => {
        const parsed = config.appsRootSchema.parse(configInputs[name]);
        return { name, input: configInputs[name], output: JSON.parse(JSON.stringify(appsRoutes.redact(parsed.apps[0]))) };
    }));

// ── WebSocket message boundaries ─────────────────────────────────────────────
const commentTexts = ["hi", "  hi  ", "a\r\nb", "a\t\t\tb", "a\u0001b", "a    b", "", "   ", "\n\n", "あ".repeat(200), "あ".repeat(201),
    "😀".repeat(200), "😀".repeat(201), " x\u0000 ", "a\u007fb", "　x　", "a \n b"];
const labels = ["Jump", "Big Jump", "  Lead  ", "A/B", "Ünïcode Name", "Tab\tName", "full　width", "x--y", "MiXeD 123", ""];
const pointers: unknown[] = [
    { type: "pointer", appId: "a", x: 0, y: 1, button: "left", phase: "down" },
    { type: "pointer", appId: "a", x: 0.5, y: 0.25, button: "middle", phase: "move", extra: 1 },
    { type: "pointer", appId: "a", x: 1.01, y: 0, button: "left", phase: "down" },
    { type: "pointer", appId: "a", x: -0.1, y: 0, button: "left", phase: "down" },
    { type: "pointer", appId: "", x: 0, y: 0, button: "left", phase: "down" },
    { type: "pointer", appId: "a", x: "0", y: 0, button: "left", phase: "down" },
    { type: "pointer", appId: "a", x: 0, y: 0, button: "back", phase: "down" },
    { type: "pointer", appId: "a", x: 0, y: 0, button: "left", phase: "hold" },
    { type: "click", appId: "a", x: 0, y: 0, button: "left", phase: "down" },
];
const audience: unknown[] = [
    { type: "audience-send", appId: "a", kind: "message", text: "hello" },
    { type: "audience-send", appId: "a", kind: "message", text: "  padded  " },
    { type: "audience-send", appId: "a", kind: "message", text: "   " },
    { type: "audience-send", appId: "a", kind: "message", text: "x".repeat(280) },
    { type: "audience-send", appId: "a", kind: "message", text: "x".repeat(281) },
    { type: "audience-send", appId: "a", kind: "message", text: "😀".repeat(140) },
    { type: "audience-send", appId: "a", kind: "message", text: "😀".repeat(141) },
    { type: "audience-send", appId: "a", kind: "stamp", stamp: "clap" },
    { type: "audience-send", appId: "a", kind: "stamp", stamp: "boo" },
    { type: "audience-send", appId: "a", kind: "stamp", stamp: "clap", text: "extra" },
    { type: "audience-send", appId: "a", kind: "message", text: "x", extra: 1 },
    { type: "audience-send", appId: "", kind: "stamp", stamp: "good" },
    { type: "audience-send", appId: "x".repeat(201), kind: "stamp", stamp: "good" },
    { type: "audience-send", appId: "a", kind: "poll" },
    { type: "audience-send", appId: "a" },
];
write("message_cases.json", {
    comments: commentTexts.map((text) => ({ text, output: comment.normalizeCommentText(text) })),
    canPost: [[null, 0], [0, 999], [0, 1000], [5, 2000]].map(([last, now]) => ({ last, now, output: comment.canPostComment(last, now) })),
    commentMessages: [
        { type: "comment", appId: "a", text: "x" }, { type: "comment", appId: "", text: "x" }, { type: "comment", appId: "a" },
        { type: "comment", appId: "a", text: 1 }, { type: "comment", appId: "a", text: "x", extra: true },
    ].map((input) => ({ input, output: comment.isCommentMessage(input) })),
    labels: labels.map((label) => ({ label, output: handler.labelToId(label) })),
    pointers: pointers.map((input) => ({ input, output: pointer.isPointerMessage(input) })),
    audience: audience.map((input) => {
        const parsed = room.AudienceSubmission.safeParse(input);
        return { input, ok: parsed.success, output: parsed.success ? parsed.data : null };
    }),
});

// ── stream preferences ───────────────────────────────────────────────────────
const prefPatches: unknown[] = [
    { intervalSec: 2 }, { maxWidth: 640 }, { intervalSec: null }, { intervalSec: 0.25, maxWidth: 0 }, { maxWidth: null },
    {}, { intervalSec: 61 }, { intervalSec: -1 }, { maxWidth: 7681 }, { maxWidth: 1.5 }, { intervalSec: "2" }, { other: 1 },
];
{
    const p = new prefs.RuntimeStreamPrefs();
    let changes = 0;
    p.on("change", () => { changes += 1; });
    write("stream_prefs_cases.json", prefPatches.map((patch) => {
        const parsed = prefs.streamPrefsPatchSchema.safeParse(patch);
        const before = changes;
        const state = parsed.success ? p.apply(parsed.data) : null;
        return { patch, ok: parsed.success, state, changed: changes !== before };
    }));
}

// ── control lease ────────────────────────────────────────────────────────────
{
    const l = new lease.ControlLease();
    const events: unknown[] = [];
    l.on("changed", (appId: string, holder: string | null) => events.push({ appId, holder }));
    const steps: Array<[string, ...string[]]> = [
        ["acquire", "a", "c1"], ["acquire", "a", "c2"], ["acquire", "a", "c1"], ["roleOf", "a", "c1"], ["roleOf", "a", "c2"],
        ["mayOperate", "a", "c2"], ["mayOperateNone", "a"], ["release", "a", "c2"], ["roleOf", "a", "c3"], ["acquire", "b", "c1"],
        ["releaseAll", "c1"], ["roleOf", "a", "c1"], ["mayOperateNone", "a"], ["acquire", "a", "c2"], ["mayOperate", "a", "c2"],
    ];
    write("control_lease_cases.json", steps.map(([op, ...args]) => {
        const before = events.length;
        let result: unknown = null;
        if (op === "acquire") result = l.acquire(args[0], args[1]);
        if (op === "release") l.release(args[0], args[1]);
        if (op === "releaseAll") l.releaseAll(args[0]);
        if (op === "roleOf") result = l.roleOf(args[0], args[1]);
        if (op === "mayOperate") result = l.mayOperate(args[0], args[1]);
        if (op === "mayOperateNone") result = l.mayOperate(args[0], null);
        return { op, args, result, events: events.slice(before) };
    }));
}

// ── audience room ────────────────────────────────────────────────────────────
{
    let t = 1000;
    let n = 0;
    const r = new room.AudienceRoom(() => t++, () => `id-${++n}`);
    for (let i = 0; i < 203; i++) r.append({ type: "audience-send", appId: "a", kind: "message", text: `m${i}` }, "観戦者 ab12");
    r.append({ type: "audience-send", appId: "b", kind: "stamp", stamp: "party" }, "x".repeat(70));
    write("audience_room.json", { a: r.history("a"), b: r.history("b"), c: r.history("c") });
}


// ── play log rows ────────────────────────────────────────────────────────────
const row = await load("playlog/playlog-row.ts");
const playerHash = await load("playlog/player-hash.ts");
{
    const inputs: unknown[] = [
        { device: "key", key: "W", down: true }, { device: "key", key: "Space", down: false }, { device: "button", id: "jump" },
        { device: "pointer", x: 0.25, y: 0.5, button: "left", phase: "down" }, { device: "pointer", x: 1, y: 0, button: "right", phase: "up" },
        { device: "pointer", x: 0.125, y: 0.75, button: "middle", phase: "move" },
    ];
    let seed = 0;
    const random = () => { seed = (seed * 9301 + 49297) % 233280; return seed / 233280; };
    const picks: number[] = [];
    const recordingRandom = () => { const v = random(); picks.push(Math.floor(v * 36)); return v; };
    write("playlog_cases.json", {
        inputs: inputs.map((input) => ({ input, name: row.inputName(input), fields: row.inputFields(input) })),
        rows: [
            { base: { run: "r", player: "p", app: "a", seq: 0, t_ms: 0, at: "2026-10-11T00:00:00.000Z" }, kind: "start", fields: { source: "ws" }, state: null },
            { base: { run: "r", player: "p", app: "a", seq: 3, t_ms: 250, at: "2026-10-11T00:00:00.250Z" }, kind: "state", fields: {}, state: { stage: "1-1", hp: 3, nested: { a: [1, 2] } } },
        ].map((c) => ({ ...c, output: row.buildRow(c.base, c.kind, c.fields, c.state) })),
        runIds: [0, 1760140800123, 1793314078909, 4102444799999].map((ms) => {
            picks.length = 0;
            const id = row.makeRunId(new Date(ms), recordingRandom);
            return { ms, picks: [...picks], id, iso: new Date(ms).toISOString() };
        }),
        runIdPattern: ["20261011-000000-abcdef", "20261011-000000-ABCDEF", "2026101-000000-abcdef", "20261011-000000-abcde", "../.salt", "20261011_000000-abcdef"]
            .map((id) => ({ id, ok: row.RUN_ID_PATTERN.test(id) })),
        hashes: [["salt", "user-1"], ["", "anon"], ["日本語", "観戦者"], ["x".repeat(100), "dev-anon"]].map(([salt, user]) => ({ salt, user, hash: playerHash.hashPlayer(salt, user) })),
    });
}

// ── keys ─────────────────────────────────────────────────────────────────────
const ergo = await load("capture/ergo-custos-client.ts");
{
    const names = ["a", "A", "z", "Z", "0", "1", "9", "Num0", "Num5", "Enter", "Return", "Escape", "Esc", "Backspace", "Tab", "Space",
        "F1", "F12", "F13", "Right", "Left", "Down", "Up", "LCtrl", "LShift", "LAlt", "LSuper", "Ctrl", "Shift", "Alt", "RShift", "ab", "", "space", "f1"];
    write("key_cases.json", names.map((name) => ({ name, hid: ergo.keyNameToErgoCode(name) ?? null })));
    write("bridge_state_cases.json", [
        { isPlaying: true, state: { a: 1 } }, { isPlaying: "yes", state: [1] }, null, [], { state: null }, { isPlaying: false, state: { nested: { b: true } } },
    ].map((body) => ({ body, output: ergo.parseBridgeState(body) })));
}

// ── bridge stream headers ────────────────────────────────────────────────────
const bridgeStream = await load("capture/bridge-stream.ts");
write("frame_header_cases.json", [
    { "x-frame-width": "1280", "x-frame-height": "720", "x-frame-pixfmt": "bgra", "x-frame-fps": "24" },
    { "x-frame-width": "603", "x-frame-height": "319", "x-frame-pixfmt": " bgra ", "x-frame-fps": "0" },
    { "x-frame-width": "1280px", "x-frame-height": "720", "x-frame-pixfmt": "bgra" },
    { "x-frame-width": "0", "x-frame-height": "720", "x-frame-pixfmt": "bgra", "x-frame-fps": "30" },
    { "x-frame-width": "1280", "x-frame-height": "720", "x-frame-pixfmt": "" },
    { "x-frame-width": "-5", "x-frame-height": "720", "x-frame-pixfmt": "bgra" },
    { "x-frame-height": "720", "x-frame-pixfmt": "bgra" },
    { "x-frame-width": " 640", "x-frame-height": "+480", "x-frame-pixfmt": "rgba", "x-frame-fps": "60.5" },
].map((headers) => ({ headers, fallback: 24, output: bridgeStream.parseFrameHeaders(headers, 24) })));

// ── ffmpeg command lines ─────────────────────────────────────────────────────
const ffmpeg = await load("capture/ffmpeg-pipeline.ts");
const audioPipeline = await load("capture/audio-pipeline.ts");
const scale = await load("capture/scale.ts");
{
    const meta = { width: 1280, height: 720, pixfmt: "bgra", fps: 24 };
    const encoders = ["x264", "nvenc", "amf", "qsv"];
    const pipeline = new ffmpeg.FfmpegPipeline();
    const capture = (extra: Record<string, unknown>) => config.appsRootSchema.parse({ apps: [app({ capture: { type: "window", windowTitle: "Game", fps: 30, ...extra } })] }).apps[0];
    const png = (width: number) => { const b = Buffer.alloc(24); b.set([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]); b.writeUInt32BE(width, 16); return b; };
    write("ffmpeg_cases.json", {
        bridgeStream: encoders.flatMap((enc) => [3_000_000, 4_000_000, 1_234_567].map((bitrate) => ({
            meta, encoder: enc, bitrate, rtpPort: 5004, args: ffmpeg.buildBridgeStreamFfmpegArgs(meta, enc, bitrate, 5004, "127.0.0.1", 96) }))),
        bridgeStreamFps1: ffmpeg.buildBridgeStreamFfmpegArgs({ ...meta, fps: 1 }, "x264", 3_000_000, 6000, "127.0.0.1", 96),
        audio: audioPipeline.buildAudioFfmpegArgs(5006),
        captureLinuxX264: pipeline.buildArgs(capture({}), { rtpPort: 5008 }),
        captureLinuxFps60: pipeline.buildArgs(capture({ fps: 60 }), { rtpPort: 5010 }),
        pngWidths: [png(1920), png(64), Buffer.from("not a png at all, nope!!"), Buffer.alloc(10)].map((b) => ({ hex: b.toString("hex"), width: scale.readPngWidth(b) ?? null })),
    });
}

// ── ICE servers ──────────────────────────────────────────────────────────────
const ice = await load("capture/ice-servers.ts");
write("ice_cases.json", {
    stun: [undefined, "", "stun:a:3478", " stun:a , stun:b ,, "].map((value) => ({ value: value ?? null, output: ice.stunServersFromEnv({ CUSTOS_STUN_URLS: value }) })),
    turn: [
        { iceServers: [{ urls: ["stun:x:3478", "turn:x:3478?transport=udp", "turn:x:53?transport=udp", "turns:x:5349?transport=tcp", "turn:x:53"], username: "u", credential: "c" }] },
        { iceServers: { urls: "turn:y:3478", username: "u2", credential: "c2" } },
        { iceServers: [{ urls: ["turn:z:3478"], username: 1, credential: "c" }] },
        { iceServers: [{ urls: [], username: "u", credential: "c" }] },
        {}, null, { iceServers: "turn:x" },
    ].map((body) => ({ body, output: ice.parseTurnResponse(body) })),
    flatten: ice.toWeriftIceServers([{ urls: ["stun:a:1", "turn:b:2?transport=udp", "turn:c:3?transport=tcp", "turns:d:5349"], username: "u", credential: "c" }, { urls: ["stun:e:1"] }]),
});

// ── H.264 packetizer / bridge frames / feedback policy ───────────────────────
const packetizer = await load("capture/h264-rtp-packetizer.ts");
const bridgeFrames = await load("capture/bridge-h264-frames.ts");
const policy = await load("capture/video-feedback-policy.ts");
{
    const nal = (type: number, size: number) => { const b = Buffer.alloc(size); b[0] = type; for (let i = 1; i < size; i++) b[i] = i % 251; return b; };
    const au = Buffer.concat([Buffer.from([0, 0, 0, 1]), nal(0x67, 20), Buffer.from([0, 0, 1]), nal(0x68, 6), Buffer.from([0, 0, 0, 1]), nal(0x65, 3000), Buffer.from([0, 0])]);
    const p = new packetizer.H264RtpPacketizer(96, 1200, { initialSequence: 65534, ssrc: 0x12345678 });
    const packets = p.packetize(au, 1_234_567_890n).map((b: Buffer) => b.toString("hex"));
    const small = new packetizer.H264RtpPacketizer(96, 1200, { initialSequence: 7, ssrc: 1 }).packetize(Buffer.from([0, 0, 1, 0x41, 1, 2, 3]), -5_000n).map((b: Buffer) => b.toString("hex"));
    const frame = (len: number, ts: bigint, flags: number) => { const h = Buffer.alloc(13); h.writeUInt32BE(len, 0); h.writeBigInt64BE(ts, 4); h[12] = flags; return Buffer.concat([h, Buffer.alloc(len, 0xab)]); };
    const stream = Buffer.concat([frame(5, 1000n, 1), frame(0, 0n, 0), frame(3, -7n, 2)]);
    const reader = new bridgeFrames.BridgeH264FrameReader();
    const chunks = [stream.subarray(0, 7), stream.subarray(7, 20), stream.subarray(20)].map((c) => reader.push(Buffer.from(c)).map((f: { data: Buffer; timestampUs: bigint; keyframe: boolean }) =>
        ({ data: f.data.toString("hex"), timestampUs: f.timestampUs.toString(), keyframe: f.keyframe })));
    const fb = new policy.VideoFeedbackPolicy({ initial: 3_000_000, min: 300_000, max: 6_000_000 });
    const steps: Array<[string, string, number, number]> = [
        ["estimate", "a", 2_000_000, 0], ["estimate", "a", 1_900_000, 100], ["estimate", "b", 5_000_000, 200], ["estimate", "a", 5_500_000, 300],
        ["estimate", "a", 5_500_000, 4_299], ["estimate", "a", 5_600_000, 4_300], ["estimate", "a", 100_000, 4_400], ["remove", "a", 0, 4_500],
        ["estimate", "b", 9_000_000, 5_000], ["estimate", "b", 9_000_000, 9_000], ["estimate", "c", -1, 9_100], ["remove", "zzz", 0, 9_200],
        ["keyframe", "", 0, 0], ["keyframe", "", 0, 499], ["keyframe", "", 0, 500], ["keyframe", "", 0, 999],
    ];
    write("h264_cases.json", {
        accessUnit: au.toString("hex"), packets, small,
        nals: packetizer.splitAnnexB(au).map((n: Buffer) => n.toString("hex")),
        frameStream: stream.toString("hex"), frameChunks: [7, 20], frames: chunks,
        policy: steps.map(([op, id, bps, now]) => ({ op, id, bps, now,
            result: op === "estimate" ? fb.updateEstimate(id, bps, now) : op === "remove" ? fb.removeSession(id, now) : fb.shouldRequestKeyframe(now) })),
    });
}

// ── unityctl ─────────────────────────────────────────────────────────────────
const uargs = await load("unity/unityctl-args.ts");
const urender = await load("unity/unityctl-render.ts");
const ucontract = await load("unity/bridge-contract.ts");
const uport = await load("unity/bridge-port.ts");
const utarget = await load("unity/bridge-target.ts");
{
    const argvs = [["status"], ["compile", "--wait", "--json"], ["log", "--since", "5", "--level", "error", "--limit", "50"], ["hierarchy", "--depth", "3", "--root", "Main Camera"],
        ["capture", "--caption", "ファー", "--source", "scene"], ["status", "--bridge", "10.0.0.2:17779"], ["status", "--bridge", "17779"], ["status", "--bridge", "myhost"],
        ["status", "--bridge", "h:0"], ["status", "--token", "t"], [], ["nope"], ["status", "--bogus"], ["log", "--since"], ["log", "--since", "--json"], ["log", "--limit", "0"],
        ["log", "--limit", "1.5"], ["log", "--since", "1e2"], ["hierarchy", "--depth", "-1"], ["log", "--limit", " 7 "]];
    const status = { unityVersion: "6000.0.1f1", projectPath: "/p", isPlaying: true, isPaused: true, isCompiling: true, isReloading: true, activeScene: "", dirtyScenes: ["A", "B"] };
    const compile = { isCompiling: false, errorCount: 1, warningCount: 1, finishedAt: "",
        errors: [{ file: "Assets/A.cs", line: 12, column: 3, message: "boom", assembly: "Game.Editor" }], warnings: [{ file: "", line: 0, column: 0, message: "w", assembly: "" }] };
    write("unity_cases.json", {
        args: argvs.map((argv) => { try { return { argv, ok: true, value: uargs.parseUnityctlArgs(argv) }; } catch (e) { return { argv, ok: false, error: (e as Error).message }; } }),
        render: {
            status: urender.renderStatus(status), statusEdit: urender.renderStatus({ ...status, isPlaying: false, isCompiling: false, isReloading: false, activeScene: "Main", dirtyScenes: [] }),
            compile: urender.renderCompileStatus(compile), compiling: urender.renderCompileStatus({ ...compile, isCompiling: true }),
            clean: urender.renderCompileStatus({ ...compile, errorCount: 0, warningCount: 0 }), exit: [urender.compileExitCode(compile), urender.compileExitCode({ ...compile, errorCount: 0 })],
            log: urender.renderLog({ entries: [{ seq: 1, ts: "t", level: "error", message: "m", stackTrace: "" }, { seq: 2, ts: "u", level: "log", message: "n", stackTrace: "" }], nextSeq: 3 }),
            emptyLog: urender.renderLog({ entries: [], nextSeq: 9 }), json: urender.renderJson({ a: [1, { b: "c" }], d: {}, e: [], f: "日本" }),
        },
        phases: [[true, 0, true, true], [true, 0, true, false], [true, 5000, false, false], [true, 90000, false, false], [false, 0, false, false], [false, 0, true, false]]
            .map(([ever, ms, reachable, compiling]) => ({ ever, ms, reachable, compiling,
                phase: ucontract.nextCompileWaitPhase({ everReachable: ever, unreachableMs: ms, unreachableBudgetMs: 90000 }, reachable, compiling) })),
        ports: ["17779\n", "", "abc", "0", "70000", " 17780 ", "65535"].map((text) => ({ text, port: uport.parseBridgePortFile(text) })),
        targets: (() => {
            const configs = [{ id: "u", inAppBridge: { kind: "unity", host: "127.0.0.1", port: 17778 } }, { id: "e", inAppBridge: { kind: "ergo", host: "127.0.0.1", port: 5201 } },
                { id: "plain" }, { id: "h", inAppBridge: { kind: "unity", host: "", port: 9 } }];
            return ["u", "e", "plain", "nope", "", "h"].map((id) => ({ id, target: utarget.resolveBridgeTarget(configs, id) }));
        })(),
    });
}

// ── device farm ──────────────────────────────────────────────────────────────
const farmSchema = await load("farm/schema.ts");
const farmPool = await load("farm/pool.ts");
{
    const farmConfig = { version: 1, heartbeatTimeoutSec: 30, hosts: [{ id: "host", tokenEnv: "HOST_TOKEN" }], devices: [
        { id: "pixel", hostId: "host", platform: "android", serial: " a ", model: "Pixel", osVersion: "16", tags: [" t1 "] },
        { id: "iphone", hostId: "host", platform: "ios", serial: "b", model: "iPhone", osVersion: "26" }] };
    let stateText = JSON.stringify({ version: 1, devices: {} });
    const store = { transaction(action: (s: unknown) => unknown) {
        const state = farmSchema.farmStateSchema.parse(JSON.parse(stateText));
        const before = JSON.stringify(state);
        const result = action(state);
        const after = JSON.stringify(farmSchema.farmStateSchema.parse(state));
        if (after !== before) stateText = after;
        return result;
    } };
    let now = 1_000_000;
    let ids = 0;
    const pool = new farmPool.DevicePool(farmConfig, store, () => now, () => `00000000-0000-4000-8000-${String(++ids).padStart(12, "0")}`);
    const steps: Array<{ op: string; args: unknown[]; advance?: number }> = [
        { op: "list", args: [] },
        { op: "report", args: ["host", { devices: [{ id: "pixel", connected: true }, { id: "iphone", connected: true }] }] },
        { op: "acquire", args: ["pixel", "alice", 60] },
        { op: "report", args: ["host", { devices: [{ id: "pixel", connected: true, readyGeneration: 1 }, { id: "iphone", connected: true, readyGeneration: 1 }] }] },
        { op: "acquire", args: ["pixel", "alice", 60] },
        { op: "acquire", args: ["pixel", "bob", 60] },
        { op: "renew", args: ["pixel", "bob", "00000000-0000-4000-8000-000000000001", 60] },
        { op: "renew", args: ["pixel", "alice", "00000000-0000-4000-8000-000000000001", 120], advance: 1000 },
        { op: "release", args: ["pixel", "alice", "00000000-0000-4000-8000-000000000009"] },
        { op: "release", args: ["pixel", "alice", "00000000-0000-4000-8000-000000000001"] },
        { op: "acquire", args: ["iphone", "  ", 60] },
        { op: "acquire", args: ["nope", "alice", 60] },
        { op: "report", args: ["other", { devices: [] }] },
        { op: "report", args: ["host", { devices: [{ id: "pixel", connected: true }] }] },
        { op: "acquire", args: ["iphone", "carol", 10] },
        { op: "list", args: [], advance: 10_000 },
        { op: "report", args: ["host", { devices: [{ id: "pixel", connected: false }, { id: "iphone", connected: true, readyGeneration: 3 }] }] },
        { op: "list", args: [], advance: 31_000 },
    ];
    const results = steps.map((step) => {
        now += step.advance ?? 0;
        try {
            const value = (pool as Any)[step.op](...step.args);
            return { ...step, ok: true, value: JSON.parse(JSON.stringify(value)), state: JSON.parse(stateText) };
        } catch (e) {
            return { ...step, ok: false, error: (e as { code?: string }).code ?? "error", state: JSON.parse(stateText) };
        }
    });
    const schemaCases = [
        farmConfig, { ...farmConfig, extra: 1 }, { ...farmConfig, version: 2 }, { ...farmConfig, heartbeatTimeoutSec: 9 },
        { ...farmConfig, hosts: [] }, { ...farmConfig, hosts: [{ id: "host", tokenEnv: "lower" }] },
        { ...farmConfig, devices: [...farmConfig.devices, { ...farmConfig.devices[0], id: "alias" }] },
        { ...farmConfig, devices: [{ ...farmConfig.devices[0], hostId: "ghost" }] },
        { ...farmConfig, devices: [{ ...farmConfig.devices[0], id: "Pixel" }] },
        { ...farmConfig, devices: [{ ...farmConfig.devices[0], serial: "   " }] },
        { ...farmConfig, devices: [{ ...farmConfig.devices[0], model: "x".repeat(161) }] },
    ].map((input) => { const r = farmSchema.farmConfigSchema.safeParse(input); return { input, ok: r.success, output: r.success ? r.data : null }; });
    const bodies = {
        report: [{ devices: [] }, { devices: [{ id: "a", connected: true, readyGeneration: 1 }] }, { devices: [{ id: "a", connected: "yes" }] }, { devices: [], x: 1 },
            { devices: [{ id: "a", connected: true, readyGeneration: 0 }] }, {}],
        acquire: [{ ttlSec: 10 }, { ttlSec: 1800 }, { ttlSec: 9 }, { ttlSec: 1801 }, { ttlSec: 60, ownerId: "bob" }, { ttlSec: 60.5 }, {}],
        release: [{ leaseId: "00000000-0000-4000-8000-000000000001" }, { leaseId: "not-a-uuid" }, { leaseId: "00000000-0000-4000-8000-000000000001", x: 1 }],
        renew: [{ leaseId: "00000000-0000-4000-8000-000000000001", ttlSec: 60 }, { leaseId: "00000000-0000-4000-8000-000000000001" }],
    };
    write("farm_cases.json", { config: farmConfig, steps: results, schema: schemaCases, bodies: {
        report: bodies.report.map((b) => ({ body: b, ok: farmSchema.hostReportSchema.safeParse(b).success })),
        acquire: bodies.acquire.map((b) => ({ body: b, ok: farmSchema.acquireSchema.safeParse(b).success })),
        release: bodies.release.map((b) => ({ body: b, ok: farmSchema.releaseSchema.safeParse(b).success })),
        renew: bodies.renew.map((b) => ({ body: b, ok: farmSchema.renewSchema.safeParse(b).success })),
    } });
}

// ── runner: bare executable resolution (POSIX paths) ─────────────────────────
const runnerModule = await load("apps/runner.ts");
write("resolve_bare_exe_cases.json", [["adventurecube.exe", "/abs/build/Debug"], ["./adventurecube.exe", "/x"], ["bin/adventurecube.exe", "/x"], ["/usr/bin/foo", "/w"],
    ["cmake", "/x"], ["run.bat", "/tmp/x"], ["Tool.EXE", "/t"], ["x.com", "/c"], ["noext", "/n"], ["a.exe", "rel/dir"]]
    .map(([cmd, cwd]) => ({ cmd, cwd, runnerDir: "/runner", output: String(runnerModule.resolveBareExe(cmd, cwd)).replace(process.cwd(), "/runner") })));

console.log(`golden written to ${out}`);
void attempt;
