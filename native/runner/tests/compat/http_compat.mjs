#!/usr/bin/env node
// HTTP / WebSocket compatibility check between the former Node runner (custos/src/main.ts) and
// odeum-runner. Node 22+ standard library only (fetch, WebSocket); no npm packages.
//
//   node native/runner/tests/compat/http_compat.mjs record <out.json> -- <server command...>
//   node native/runner/tests/compat/http_compat.mjs compare <node.json> <cpp.json>
//
// `record` starts the server with a synthetic apps.json (no real app, no external service; the
// Unity bridge address points at a closed local port), plays a fixed scenario, normalises volatile
// values (uuids, times, pids, ports) and writes the transcript. `compare` prints the differences
// and exits 1 when a difference is not listed in EXPECTED_DIFFERENCES.
import { spawn } from "node:child_process";
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createServer } from "node:net";

// Documented, intentional differences (see spec/plan/2026-10-30-runner-cpp-port.md).
const EXPECTED_DIFFERENCES = [
    // zod's error text cannot be reproduced; status and `ok` are compared instead.
    { step: "PUT /api/settings/stream (out of range)", field: "body.error" },
    { step: "PUT /api/settings/stream (array)", field: "body.error" },
    // The port refuses cross-origin writes (new CSRF guard); Node accepted them.
    { step: "POST /api/apps/demo/kill (cross-origin)", field: "status" },
    { step: "POST /api/apps/demo/kill (cross-origin)", field: "body" },
    // nosniff is added to every response.
    { step: "*", field: "headers.x-content-type-options" },
];

const freePort = () => new Promise((resolve) => {
    const s = createServer();
    s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => resolve(port)); });
});
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function normalise(value, ports) {
    if (typeof value === "string") {
        let v = value.replace(/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/g, "<uuid>")
            .replace(/\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z/g, "<iso>");
        for (const [port, name] of ports) v = v.split(String(port)).join(`<${name}>`);
        return v;
    }
    if (Array.isArray(value)) return value.map((v) => normalise(v, ports));
    if (value && typeof value === "object") {
        const out = {};
        for (const [k, v] of Object.entries(value)) {
            if (["lastChangeAt", "ts", "time", "createdAt", "updatedAt"].includes(k) && typeof v === "number") out[k] = "<time>";
            else if (k === "pid" && typeof v === "number") out[k] = "<pid>";
            else out[k] = normalise(v, ports);
        }
        return out;
    }
    return value;
}

async function record(outFile, command) {
    const dir = mkdtempSync(join(tmpdir(), "runner-compat-"));
    const backend = await freePort();
    const frontend = await freePort();
    const closedBridge = await freePort();  // nothing listens: the Unity bridge is "down"
    const ports = [[backend, "backend"], [frontend, "frontend"], [closedBridge, "bridge"]];
    mkdirSync(join(dir, "plays", "demo"), { recursive: true });
    writeFileSync(join(dir, "plays", "demo", "20261011-000000-abcdef.jsonl"), "{\"seq\":0}\n");
    writeFileSync(join(dir, "apps.json"), JSON.stringify({ version: 1, apps: [
        { id: "demo", name: "Demo", run: { cwd: dir, cmd: "node", args: ["-e", "console.log('hi')"] },
          input: { buttons: [{ label: "Big Jump", key: "Space" }, { label: "Quit", action: "kill" }] } },
        { id: "u", name: "Unity", run: { cwd: dir, cmd: "node", args: ["-e", "0"] }, inAppBridge: { kind: "unity", port: closedBridge }, playLog: {} },
    ] }));
    const env = { ...process.env, CUSTOS_PORT: String(backend), CUSTOS_FRONTEND_PORT: String(frontend), CUSTOS_HOST: "127.0.0.1",
        CUSTOS_APPS_FILE: join(dir, "apps.json"), CUSTOS_PLAYLOG_DIR: join(dir, "plays"), CUSTOS_STUN_URLS: "stun:stun.example.invalid:3478",
        CUSTOS_SCREENSHOT_INTERVAL_SEC: "0", CUSTOS_PLAYLOG_SALT: "compat-salt" };
    for (const k of ["CUSTOS_OPEN", "CERNERE_URL", "CUSTOS_AUTH_REQUIRED", "CUSTOS_FARM_CONFIG", "CUSTOS_TURN_KEY_ID", "CUSTOS_TURN_KEY_API_TOKEN"]) delete env[k];
    // Own process group, so wrappers such as `npx tsx` are stopped with the server they start.
    const server = spawn(command[0], command.slice(1), { env, stdio: ["ignore", "ignore", "ignore"], detached: true });
    const base = `http://127.0.0.1:${backend}`;
    for (let i = 0; i < 100; i++) {
        try { if ((await fetch(`${base}/api/health`)).ok) break; } catch { /* starting */ }
        await sleep(100);
    }
    const steps = [];
    const http = async (name, method, path, body, headers = {}) => {
        const res = await fetch(base + path, { method, headers: { "content-type": "application/json", ...headers }, body });
        const text = await res.text();
        let parsed = text;
        try { parsed = JSON.parse(text); } catch { /* not JSON */ }
        const type = (res.headers.get("content-type") ?? "").toLowerCase().replace(/\s+/g, "");
        const isText = /^(application\/json|text\/plain)/.test(type);
        steps.push(normalise({ step: name, status: res.status, headers: { "content-type": type, "cache-control": res.headers.get("cache-control"),
            "x-content-type-options": res.headers.get("x-content-type-options") },
            body: isText || type.includes("ndjson") ? parsed : { bytes: Buffer.byteLength(text) } }, ports));
    };
    await http("GET /api/health", "GET", "/api/health");
    await http("GET /api/apps", "GET", "/api/apps");
    await http("GET /api/apps/demo", "GET", "/api/apps/demo");
    await http("GET /api/apps/demo/status", "GET", "/api/apps/demo/status");
    await http("GET /api/apps/nope", "GET", "/api/apps/nope");
    await http("GET /api/apps/nope/status", "GET", "/api/apps/nope/status");
    await http("POST /api/apps/nope/build", "POST", "/api/apps/nope/build");
    await http("POST /api/apps/demo/build (no build)", "POST", "/api/apps/demo/build");
    await http("POST /api/apps/demo/test (no test)", "POST", "/api/apps/demo/test");
    await http("POST /api/apps/demo/kill", "POST", "/api/apps/demo/kill");
    await http("POST /api/apps/demo/kill (cross-origin)", "POST", "/api/apps/demo/kill", undefined, { origin: "http://evil.example" });
    await http("POST /api/apps/demo/screenshot (no capture)", "POST", "/api/apps/demo/screenshot");
    await http("POST /api/apps/demo/rtc/offer (no capture)", "POST", "/api/apps/demo/rtc/offer", JSON.stringify({ sdp: "v=0" }));
    await http("POST /api/apps/u/rtc/offer (no sdp)", "POST", "/api/apps/u/rtc/offer", JSON.stringify({}));
    await http("POST /api/apps/_/rtc/close (no id)", "POST", "/api/apps/_/rtc/close", JSON.stringify({}));
    await http("POST /api/apps/_/rtc/close (unknown)", "POST", "/api/apps/_/rtc/close", JSON.stringify({ sessionId: "x" }));
    await http("GET /api/apps/_/rtc/ice-servers", "GET", "/api/apps/_/rtc/ice-servers");
    await http("GET /api/settings/stream", "GET", "/api/settings/stream");
    await http("PUT /api/settings/stream", "PUT", "/api/settings/stream", JSON.stringify({ maxWidth: 640, intervalSec: 2 }));
    await http("PUT /api/settings/stream (out of range)", "PUT", "/api/settings/stream", JSON.stringify({ intervalSec: 61 }));
    await http("PUT /api/settings/stream (array)", "PUT", "/api/settings/stream", "[1]");
    await http("PUT /api/settings/stream (invalid JSON)", "PUT", "/api/settings/stream", "nope");
    await http("PUT /api/settings/stream (clear)", "PUT", "/api/settings/stream", JSON.stringify({ intervalSec: null }));
    await http("GET /api/playlogs", "GET", "/api/playlogs");
    await http("GET /api/playlogs?appId=demo", "GET", "/api/playlogs?appId=demo");
    await http("GET /api/playlogs/demo/<run>", "GET", "/api/playlogs/demo/20261011-000000-abcdef");
    await http("GET /api/playlogs/demo/bad", "GET", "/api/playlogs/demo/bad");
    await http("GET /api/unity/demo/status (no bridge)", "GET", "/api/unity/demo/status");
    await http("GET /api/unity/u/status (bridge down)", "GET", "/api/unity/u/status");
    await http("POST /api/unity/u/play (bridge down)", "POST", "/api/unity/u/play");
    await http("GET /", "GET", "/");
    await http("GET /js/app.js", "GET", "/js/app.js");
    await http("GET /nope", "GET", "/nope");

    const messages = [];
    const ws = new WebSocket(`ws://127.0.0.1:${frontend}/ws`);
    ws.onmessage = (e) => messages.push(JSON.parse(e.data));
    await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
    await sleep(200);
    ws.send(JSON.stringify({ type: "subscribe", appId: "demo" }));
    ws.send(JSON.stringify({ type: "subscribe", appId: "nope" }));
    ws.send("not json");
    ws.send(JSON.stringify({ type: "key", appId: "demo", key: "W", down: true }));
    ws.send(JSON.stringify({ type: "button", appId: "demo", id: "quit" }));
    ws.send(JSON.stringify({ type: "button", appId: "demo", id: "nope" }));
    ws.send(JSON.stringify({ type: "pointer", appId: "demo", x: 2, y: 0, button: "left", phase: "down" }));
    ws.send(JSON.stringify({ type: "comment", appId: "demo", text: "  nice\n run " }));
    await sleep(300);
    await fetch(`${base}/api/apps/demo/run`, { method: "POST" });
    await sleep(1500);
    ws.close();
    steps.push(normalise({ step: "ws transcript", messages: messages.map((m) => (m.type === "log" ? { ...m, text: m.text.trim() } : m)) }, ports));

    try { process.kill(-server.pid, "SIGTERM"); } catch { server.kill("SIGTERM"); }
    await sleep(500);
    try { process.kill(-server.pid, "SIGKILL"); } catch { /* already gone */ }
    rmSync(dir, { recursive: true, force: true });
    writeFileSync(outFile, JSON.stringify(steps, null, 2) + "\n");
    console.log(`recorded ${steps.length} steps to ${outFile}`);
}

function diff(a, b, path, out) {
    if (JSON.stringify(a) === JSON.stringify(b)) return;
    if (a && b && typeof a === "object" && typeof b === "object" && !Array.isArray(a) && !Array.isArray(b)) {
        for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) diff(a[k], b[k], path ? `${path}.${k}` : k, out);
        return;
    }
    out.push({ field: path, node: a, cpp: b });
}

function compare(nodeFile, cppFile) {
    const node = JSON.parse(readFileSync(nodeFile, "utf8"));
    const cpp = JSON.parse(readFileSync(cppFile, "utf8"));
    let unexpected = 0;
    for (let i = 0; i < Math.max(node.length, cpp.length); i++) {
        const step = node[i]?.step ?? cpp[i]?.step;
        const out = [];
        diff(node[i], cpp[i], "", out);
        for (const d of out) {
            const expected = EXPECTED_DIFFERENCES.some((e) => (e.step === step || e.step === "*") && (d.field === e.field || d.field.startsWith(`${e.field}.`)));
            if (!expected) unexpected++;
            console.log(`${expected ? "expected  " : "DIFFERENT "} ${step} :: ${d.field}\n    node: ${JSON.stringify(d.node)}\n    cpp:  ${JSON.stringify(d.cpp)}`);
        }
    }
    console.log(unexpected === 0 ? "compatible (only expected differences)" : `${unexpected} unexpected difference(s)`);
    process.exit(unexpected === 0 ? 0 : 1);
}

const [mode, ...rest] = process.argv.slice(2);
if (mode === "record") {
    const sep = rest.indexOf("--");
    await record(rest[0], rest.slice(sep + 1));
} else if (mode === "compare") {
    compare(rest[0], rest[1]);
} else {
    console.error("usage: http_compat.mjs record <out.json> -- <server command...> | compare <node.json> <cpp.json>");
    process.exit(2);
}
