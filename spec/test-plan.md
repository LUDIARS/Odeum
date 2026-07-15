# Spectator test plan

Tests are derived from the desktop application's critical risks.

## Deterministic unit coverage

- Playtime never includes inactive intervals and never exceeds elapsed time.
- Repeated active/inactive transitions are idempotent.
- Capture anchors use injected UTC time and identifiers.
- Draft manifests preserve the capture anchor and media outcome.
- Self-reported text is preserved verbatim and ordered by video offset.
- Raw data rejects annotations outside the video duration.
- Raw data preserves comment/positive/negative kinds independently from display text.

## Windows integration checks

- Catalog excludes Spectator itself and invisible/untitled windows.
- Attachment follows move/resize and detaches after target destruction.
- Every registered hook and hotkey is released when its owner is disposed.
- Snapshot failures are explicit for minimized, destroyed, or protected targets.
- SQLite v1 data upgrades to the reaction-capable v2 schema without losing drafts.
- Deleting a local draft cascades to its reaction annotations.
- Standalone configuration loads without Volputas URLs, tokens, or login provider.
- MP4/MKV/WebM import copies rather than moves the source, persists duration/hash, and creates a reopenable local draft.
- Volputas mode still rejects insecure remote HTTP and missing required settings.

## Video integration checks

- OBS authentication challenge is calculated from password, salt, and challenge.
- Requests are correlated by request ID.
- Disconnects fail pending requests rather than leaving them suspended.
- Replay save completes only after `ReplayBufferSaved` provides a readable path.

The executable core test project is dependency-free so it remains runnable without a package restore network. Windows/OBS integration requires a local smoke matrix and is not replaced by mocks.

The OBS release gate starts with `eng/Test-Obs-Prerequisites.ps1 -RequireRunning`, then connects from Spectator and saves a replay from the selected game scene. A machine without a running OBS WebSocket is reported as not ready; it is never treated as a passing recording test.

The critical standalone smoke path is: import a recorded video, seek to distinct offsets, add one positive and one negative stamp with comments, restart Spectator, reopen the draft, export JSON, and import the resulting `{ utterances, gameId, sourceRef }` document into the Volputas timeline importer.

`eng/Run-Standalone-Smoke.ps1` automates the non-UI portion with a real generated MP4: Windows duration probing, managed copy, SQLite reopen, both stamp kinds, SHA-256, and raw JSON export. Window selection, seeking, and button interaction remain a manual desktop smoke because they cross the WPF and native-window boundary.
