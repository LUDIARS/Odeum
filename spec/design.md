# Spectator implementation design

## Purpose and priorities

Spectator exists to preserve the exact moment a player forms an impression, including the player's own time-aligned words, and associate it with target-window media and playtime. Priorities are, in order:

1. Do not interfere with game input or modify the game process.
2. Capture only the explicitly selected target and never hide degraded media capability.
3. Release every native handle, hook, socket, timer, and file on normal and exceptional paths.
4. Keep self-reported reaction data and drafts recoverable without authentication or remote services.
5. Keep Volputas synchronization optional and downstream of the local source of truth.

## Stack decision

LUDIARS normally selects Rust and Tauri for desktop applications. Spectator intentionally deviates:

- The product is Windows-only in its first release.
- HWND tracking, out-of-process WinEvent hooks, global hotkeys, per-monitor DPI, WGC, WASAPI, and Media Foundation dominate the implementation.
- A WebView UI would still require a native bridge for the critical path and would add another focus and GPU boundary.

The shell therefore uses .NET 8 WPF. Deterministic domain logic lives in `Spectator.Core`. Windows integration lives in `Spectator.Windows`. Built-in continuous video capture will be isolated in a C++20/C++/WinRT component once its quality spike passes; all video implementations conform to `IVideoCaptureBackend`.

## Dependency direction

```text
Spectator.Windows ──> Spectator.Core
Native integrations ──> Spectator.Core contracts
Spectator.Core ──> no UI or OS project
```

## Integration boundary

`integrationMode` is an explicit configuration boundary:

- `standalone`: the default. Spectator creates no HTTP client, login coordinator, or sync worker. Capture, annotation, local persistence, and JSON export remain available without authentication.
- `volputas`: requires API URL, web URL, and login provider at startup. Existing authenticated post synchronization is enabled in addition to all standalone behavior.

Missing Volputas configuration never causes an implicit fallback. Users select standalone mode intentionally.

## Current capture flow

1. The user selects a visible top-level window.
2. Spectator starts a local play session and subscribes to target window events.
3. `Ctrl+Shift+F8` creates one immutable capture anchor.
4. The target screenshot and configured video backend run against the same anchor.
5. Spectator writes media first, then atomically publishes a JSON draft manifest.
6. A video failure is recorded and shown; it is never silently reported as success.

## Self-reported reaction flow

1. The player imports an MP4, MKV, or WebM recording. Spectator copies it into managed local storage and creates a local review draft; the source file is never moved.
2. During playback or after seeking, the player records a free-form comment, a positive stamp (`ここ良かった`), or a negative stamp (`ここ悪かった`) at the current `videoOffsetMs`.
3. Spectator stores the explicit `reactionKind`, unmodified text, UTC recording time, and video offset in local SQLite.
4. Export produces versioned UTF-8 JSON with the video SHA-256 as `sourceRef`.
5. The document exposes the entries as `utterances`, so the existing Volputas timeline importer can consume it while ignoring fields it does not yet derive. Emotion vectors and curves remain derived Volputas data; Spectator does not claim to infer the player's emotions.

## Resource ownership

- `WindowAttachment`: owns and disposes all WinEvent hooks it creates.
- `GlobalHotKeyRegistration`: owns the hotkey ID and WPF message hook.
- `ObsWebSocketClient`: owns its WebSocket, receive loop cancellation, and pending requests.
- `MainWindow`: owns UI timers and application-scoped integrations and disposes them on close.
