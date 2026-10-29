# Custos source copy

`custos/` is the complete tracked source tree of Custos commit
`9993aa9cd73de610c2b49808d14388b012c0a40f`, imported by `git archive` on 2026-10-17.
All 249 tracked files were originally imported byte-for-byte. The original Custos repository is unchanged.
Git metadata, untracked logs, installed dependencies, generated builds and local secrets are not copied.

This snapshot retains its original product names and configuration for provenance. It is not
registered as a second Custos service and must not be started with the original machine-specific
application configuration. Odeum's active capture, transport, composition and recording entry points
are `native/presenter`, `native/relay`, and `native/program`; the existing native pipeline is retained
instead of replacing it with the older runner's FFmpeg/OBS paths. Odeum has no runtime dependency
on the original Custos checkout. Future source adaptations belong to this repository only.

## Odeum adaptations

2026-10-26: the remote-play frontend in this copy adds on-device eye/face
tracking for the active player, including phone front-camera selection,
calibration and local gaze overlay. See `spec/feature/mobile-play-eye-tracking.md`.
This adapts the preserved player source; it does not register or launch a second
Custos service, replace the native relay, or claim this page is deployed.
