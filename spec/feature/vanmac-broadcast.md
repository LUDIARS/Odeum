# SPEC-VANMAC-BROADCAST

The broadcast-production boundary prepares a macOS OBS parent with four remote
Cocoiru SRT inputs and a return of the complete OBS program. It owns configuration
generation and non-overwriting OBS installation; Excubitor owns process lifecycle.
The existing WebRTC relay and its bootstrap contract are independent.

Invariants:
- The parent is an explicitly selected, locally assigned Tailscale IPv4 address.
- Each sender publishes exactly one input and reads only the final program.
- The parent reads all inputs and is the only program publisher. All SRT paths
  require generated encryption passphrases; no other protocol listener is enabled.
- Configuration files containing credentials stay private and out of Git/log output.
- Preparation never starts an application, rotates existing credentials, modifies
  firewall rules, installs OS packages or overwrites a completed/partial setup.
- OBS installation refuses an existing profile/collection or running OBS and does
  not change other profiles. Its newly created profile is rolled back on failure.
- The return is the actual program output, including whatever reaction source is
  configured in OBS, and never a selected raw input or local sender preview.

The native sender and capture-excluded preview are owned by Cocoiru. MediaMTX
provides routing, not rendering or a reaction service. This implementation does
not assert runtime success from static checks, Ex registration or process presence.
Deployment/runbook: [VANMAC](../../broadcast/VANMAC.md).
# Excubitor broadcast bootstrap

`excubitor.bootstrap.odeum-broadcast.json` selects `scripts/broadcast/setup.mjs`.
It requires Ex service-specific manifest support (Ex task
`actio:7b5951f6-39d2-44d5-871d-e0af3ef642e9`, local PR #2461).
The legacy root manifest continues to own WebRTC relay installation.

Setup takes no arguments, requires a macOS main checkout, installed OBS, Ex running
as the logged-in desktop user, stopped OBS and exactly one local Tailscale IPv4.
It downloads the architecture-specific MediaMTX 1.21.1 archive, checks its pinned
official release SHA-256, and extracts only the binary. No sudo, OS package
installation, daemon launch, real streaming, or YouTube credentials are involved.
Use bootstrap with `start:false` for the authorized installation-only operation.

An exclusive lock prevents simultaneous bootstrap writers. Successful retries
reuse prepared files and identical installed OBS files without rotating secrets.
Incomplete preparations, changed OBS settings, locks left by a killed process,
or host identity changes require explicit operator reconciliation; setup never
deletes these records to make a retry pass. Private temporary downloads are
removed after both success and failure. Existing OBS settings remain untouched.

The router records no persistent media. Export/import use a versioned empty
`odeum-broadcast` envelope with `policy=fresh-host-identities`; export refuses
overwrite, import checks SHA-256 and strict identity/shape and changes no local
configuration. Host addresses, credentials, sender URLs, YouTube keys and local
OBS edits are excluded. This is not an OBS backup facility. Destination setup
creates new identities; migrating OBS edits needs a separate explicit workflow.

Verification for this change: syntax/JSON and patch checks only. macOS setup,
download execution, data lifecycle, service starts and streaming await authorized
runtime validation. Implementation dependency completion does not imply that
VANMAC has received the updated Ex runtime.
