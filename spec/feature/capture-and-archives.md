# Network sources and recording archives

Request: neco, 2026-10-17. Custos must remain unchanged; copy its entire code into Odeum,
offer window/full-display/region sources, compose at the production machine, integrate Project GLab,
record the program in the background and distribute the recording later without OBS or HDMI.

## Evidence and implementation boundary

- Custos source: `9993aa9cd73de610c2b49808d14388b012c0a40f`, complete tracked copy in `custos/`.
- Odeum base: `adc2b5c`; existing native relay/program/presenter are retained.
- Praeforma: Custos project `01M3FVMFAGZXJ6BBC8QQP7NE1T`, UX revision 1. Odeum was absent from
  the project list. Anatomia has `custos` but no Odeum registration; inspected the native source directly.
- Source APIs: [Microsoft CopySubresourceRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion)
  and [Apple sourceRect](https://developer.apple.com/documentation/screencapturekit/scstreamconfiguration/sourcerect).
- Project GLab is provisionally interpreted as GLab project association, pending the requester's
  clarification of whether it also means a visual program frame. No visual branding asset is invented.

## Sources

Presenter offers ウインドウ / 画面全体 / 画面の一部. Region selection starts at the center 50%
of a display; arrow and size buttons adjust in 5% steps. The values are shown before sending.
Changing region stops capture until the user starts it again. No clipboard capture or automatic
permission grant is attempted. Window/display selection uses the existing enumerated target list.

Windows crops the D3D11 texture before encoding. macOS sets ScreenCaptureKit sourceRect before
encoding. Only the selected region is transmitted; crop is never implemented at the receiver.
The existing WGC/MF and ScreenCaptureKit/VideoToolbox encoders send over WebRTC to relay input slots;
odeum-program composes those inputs at the production machine.

## Recording and archive playback

Program settings add `project_id` (GLab identifier, letters/digits/hyphen/underscore, at most 80)
and `archive_directory` (default: `archives/` alongside program.json). The operator can paste the
project ID. This is local organization metadata, not an authorization grant.

Successful producer connection, live YouTube start or return start starts recording if needed.
The UI displays recording state and disk errors and offers explicit start/finalize controls.
Recording consumes the already composed H.264/AAC outputs, including reactions, independently
of YouTube connectivity. Disk writing runs on its own thread with a 32 MiB bounded queue; disk
failure or overflow fails the recording rather than silently dropping frames or blocking live capture.

Each recording owns `<root>/<projectId>/<epoch-ms>-<random>/`. It writes `program.flv.partial`,
then closes it and renames to `program.flv`. `archive.json` is atomically published only after
finalization and includes format version, projectId, durationMs and bytes. Incomplete recordings
remain distinguishable and are never offered for playback. No automatic deletion or upload occurs.

The program panel cycles through completed recordings for the selected project. “録画を YouTube 配信”
uses the existing encrypted stream-key store and RTMPS transport, reads bounded FLV tags, and paces
them on the stored timeline. Live encoder output is excluded from archive playback. Stop cancels
playback; EOF reports completion. Network retry restarts the recording from the beginning.

The archive format is FLV (H.264/AAC), for rebroadcast through Odeum; it is not a browser-native MP4
download. Web on-demand archive viewing and storage synchronization to GLab are not implemented here.

## Validation

Windows Release build of `odeum-program`, `odeum-presenter` and `odeum_program_archive_tests`
completed on 2026-10-17. Test executables were built, not run. Fixed presenter integration with
current Tela by applying WDA_EXCLUDEFROMCAPTURE directly to its native window. macOS was not
built: the local Tela checkout does not provide the existing macos_desktop_overlay.hpp dependency.
Real-device capture, long recording, A/V synchronization, RTMPS delivery and visual UI are unverified.

Registered `odeum.program.archive` covers crop bounds, path traversal, incomplete recording exclusion,
project-scoped catalogs, FLV roundtrip and corruption rejection. Tests and real capture/broadcast
must run through the authorized review/test workflow. A build is not a real-device acceptance result.
