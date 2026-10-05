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
