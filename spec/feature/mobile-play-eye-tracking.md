# Smartphone play eye tracking {#SPEC-ODEUM-PLAY-GAZE}

User: neco, 2026-10-25/26. Track eyes while playing on a smartphone in Odeum;
also support PC cameras. Camera images are not broadcast and cannot be used for
other purposes. This refines the earlier Vo capture-session interpretation.

## Ownership and lifecycle

The Odeum-owned remote-play frontend (`custos/public/js/app.js`) starts tracking
only for the server-assigned `player`, a running game, live video, connected
control socket and visible page. Observers and idle pages never acquire cameras.
Role loss, game stop, disconnect, app change, hidden page and pagehide stop the
private owner. Returning via browser history may re-acquire only when all gates
hold. Retry after failure is explicit, not a repeated permission prompt loop.

The camera requests the user-facing camera with audio disabled and rejects an
environment-facing result. PCs without a facingMode use the selected webcam.
Only the tracker sees video frames. No camera preview, MediaRecorder, canvas
export, outgoing peer track, HTTP upload, WS payload, persistent storage, image
log or identity inference is present in this path. Existing game snapshots and
recordings remain game-only. Derived samples remain local and ephemeral.

## Gaze interpretation

Face/iris landmarks produce eye-opening-normalized iris positions and head-pose
proxies. Nine targets calibrate these features to the displayed game rectangle;
letterboxing is included. No screen-gaze claim is made before calibration.
Only a fitted, finite, in-bounds estimate renders the local gaze point. Lost
eyes hide it. Failed/insufficient calibration is visible, not a heuristic pass.
Calibration blocks game pointer/key/pad input after releasing held controls.
Resize/app/lifecycle changes invalidate the calibration. Samples are bounded to
20 per target and discarded after fitting. Facial coefficients are also derived
locally, without interpreting identity or emotions.

## Assets and preparation

In the Odeum-owned `custos/` package: `npm ci`, then `npm run setup:gaze`.
The latter installs MediaPipe Tasks Vision 1.0.1 (Apache-2.0) browser/WASM assets
and the pinned float16 Face Landmarker v1 model under ignored `public/gaze-assets/`.
An offline copy can be supplied as `ODEUM_GAZE_MODEL_PATH`; the checksum must
match the same model. The expected SHA-256 is
`64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff`.
It was checked against the existing Vo deployment artifact. Runtime browser
fetches are same-origin only. There is no runtime CDN or cloud inference.
Camera access requires HTTPS (or browser-approved localhost) and user permission.

API: https://developers.google.com/edge/mediapipe/solutions/vision/face_landmarker/web_js
Camera constraints: https://developer.mozilla.org/en-US/docs/Web/API/MediaTrackConstraints/facingMode

## Deployment boundary and evidence

Base Odeum `ccaddbe`. The preserved player code was imported from Custos
`9993aa9c`; only the Odeum copy changes. Original Custos is untouched. Existing
native relay serves guest reactions and overlays, not this remote-play page.
The current Odeum catalog has no remote-play web service. Therefore this patch
alone is NOT an enabled smartphone deployment. Registration/routing of the
Odeum player and its game configuration must be completed on the actual host;
do not launch the old Custos machine configuration as a fallback.

2026-10-26 read-only Excubitor confirmation: only `odeum-broadcast`, `odeum-obs`,
`odeum-program` and `odeum-relay` are registered for Odeum. Relay is reported
stopped and the other three unknown. No play-web runtime was found; no service
was started or restarted. Static player source delivery and gaze asset setup
must be connected to the actual player host before claiming availability.

Vo tracking math/lifecycle code was adapted from local commit `9d0a05d` as owned
source; there is no build/runtime sibling dependency. Pf/An had no Odeum project
in the prior lookup; local source and the capture/archives specification are the
current evidence, not proof of execution.

## Acceptance

Static syntax compilation and diff hygiene can be checked without starting a
service. Unit cases cover role/lifecycle gates, explicit retry, late permissions,
numeric-only output, camera cleanup and bounded calibration. Tests are supplied
but not run under session policy. Actual iOS Safari, Android and PC acceptance,
HTTPS/permissions, asset delivery, touch latency, accuracy and network/storage
privacy inspection remain required. No camera or live gameplay was accessed.

## Review prerequisites (2026-10-26)

Local PR 3477 stopped at Praesidium preflight with
`native.configuration-required`; this is not an executed gaze assertion failure.
At that revision Odeum had no `praesidium.json`, while the scanner requires
translation-unit configuration for all existing native sources. Host-owned
include packages must also cover the actual native dependencies (including
OpenSSL, Boost and libdatachannel). Do not exclude native sources, stub headers,
or mark this check successful to unblock a frontend change.

The gaze guardrail file is registered in `.augur/tests.jsonl` with `always: true`
and zero recorded runs. Its runner uses the Odeum-owned
`custos/node_modules/vitest/vitest.mjs`; review setup must install the locked
dependencies in `custos` before executing it. Registry lint validates metadata,
not behavior. Native preflight repair, test execution and device acceptance
remain prerequisites to a successful review and deployment.

The user subsequently approved native review configuration and host dependency
setup. `praesidium.json` now covers all 115 translation units, host packages are
registered, and Revisor has the `install-play-dependencies` preparation case.
See [native review environment](native-review.md) for the evidence and remaining
scanner-budget/optional-Pictor-API blockers. These changes do not establish a
passing review or smartphone runtime acceptance.
