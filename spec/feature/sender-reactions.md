# SPEC-SENDER-REACTIONS

User neco (2026-10-08) selected sender-controlled visibility for questions and
impressions. Task: actio:dbf75f13-a845-4b0d-82d0-f20df07c429c.

## Wire and delivery

- `telop`: nonempty UTF-8 `text`, at most 60 Unicode scalars. Transient on-air text.
- `submission`: nonempty `text` up to 280 scalars, `category` exactly `question` or
  `impression`, required boolean `show_on_screen`. Missing or nonboolean visibility
  is rejected, not treated as permission to publish.
- Only viewers submit. The relay rebuilds `from` and `at` from authenticated identity
  and server time, rather than copying client identity fields.
- Telops and public submissions go to current participants; private submissions go
  only to the presenter socket. Neither enters reaction bursts or session listings.
- Text reactions share the existing one-message-per-three-seconds limit with comments.
- Updated presenters send `reaction.ready` with `version:1` after welcome. Only the
  presenter role may announce readiness. `presence.reaction_version` is then 1;
  otherwise 0. New text submissions are rejected until a capable presenter connects.
  Deploy relay/presenter before the updated GLab phone UI; old receivers never
  reinterpret private submissions as legacy visible comments.

## Rendering and inbox

Telops last five seconds, newest three at most, without video pause/seek/capture
changes. Existing good/stamp effects continue. Public questions/impressions use the
bounded comment display; private text never enters OverlayState. The presenter
control panel has a separate navigable inbox of the latest 100 submissions, including
the sender-selected visibility and category. It does not offer a publish override.
Inbox text exists only in memory and clears on a new connection/session. The existing
Windows capture affinity and macOS excluded application protect the control panel.

GLab renders public text and reaction effects over its video element because the
native desktop overlay is intentionally capture-excluded. This is the WebRTC viewer
path. The OBS program layer and login-free phone participation are specified in
[SPEC-PROGRAM-OVERLAY-GUEST-JOIN](program-overlay-guest-join.md); live Cocoiru/SRT/OBS
confirmation remains part of the separate LAN deployment task.

## Verification

Contract checks must reject missing visibility and malformed text/category, enforce
presenter-only readiness, and demonstrate private socket isolation. Overlay/inbox
tests cover private exclusion, public inclusion, bounds, expiry and session clearing.
Live phone/capture integration needs deployed compatible relay, presenter and GLab.
