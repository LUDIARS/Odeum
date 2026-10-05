# SPEC-VANMAC-BROADCAST

VANMAC is the macOS parent confirmed by neco. Ex peer VANMAC resolves to
Tailscale `100.99.88.97` (`ip-10-40-15-121` at the time of inspection).
This document describes preparation, not evidence of a running broadcast.

## Routes

Four Windows Cocoiru senders publish `input1` through `input4` over SRT.
MediaMTX 1.21.1 routes them to four shared OBS media sources. OBS switches or
composes them into one 1080p30 YouTube RTMPS program. A separate custom FFmpeg
recording output publishes that same final OBS program at 640x360 to `program`.
The router distributes this return to up to four Cocoiru muted corner previews.
It does not encode once per viewer. YouTube output and return output are independently
started and stopped. The return is an additional encode, not a free copy.

The profile deliberately selects software x264, not the Windows NVENC setting.
Hardware encoding can be selected explicitly in OBS after the Mac's capability is
confirmed. Text sources use FreeType/Hiragino rather than Windows GDI+. Input
audio follows scene activation; the four-way layout mixes all four input tracks.
Mute unwanted input audio and do not duplicate it in AUDIO - Program.

Any reaction layer composed in OBS is included in both outputs. This change does
not invent a reaction feed: add the real reaction source to the program scenes
before acceptance. No YouTube key or microphone is supplied by the templates.

## Prepare on VANMAC, without launching OBS or the router

Prerequisites: ordinary Odeum `main` checkout, Node, OBS at `/Applications/OBS.app`,
and an operator-provided MediaMTX **1.21.1** executable for the Mac's architecture.
MediaMTX is MIT licensed; use the official release and verify its published checksum.
The script copies the supplied binary; it does not download, execute, version-probe,
install OS packages, change firewall rules or register auto-start.

```sh
node broadcast/scripts/prepare-macos.mjs --parent-ip 100.99.88.97 --mediamtx /absolute/path/to/mediamtx
node broadcast/scripts/install-obs-macos.mjs
```

The first command requires that this IP belongs to the executing Mac. It generates
private files under `artifacts/broadcast` with fresh random credentials. An existing
directory is refused, including a partial attempt; inspect/preserve it before any
manual recovery. It never silently rotates working credentials. `prepared.json`
is written last. The installer refuses running OBS or an existing Odeum_VANMAC profile
or scene collection, and preserves all other OBS settings. Run it as the OBS user,
not root or a separate Ex daemon account. Installation does not select/start a stream.

Only SRT is enabled, bound to the specified Tailscale interface and the UDP port
declared in `excubitor.catalog.yaml`. Each sender has its own credentials, permission
to publish its single input and to read `program`; the parent alone may publish the
program. All paths require SRT encryption. Existing publishers cannot be displaced.
Unknown paths, unauthenticated users and administrative/other protocol listeners
are disabled. Authorize the declared UDP port in the tailnet/host firewall only if
required; no public Internet port forwarding is part of this setup.

Give each sender only its matching `sender-N.json` over an approved private channel.
Paste `sendUrl` and `returnUrl` into Cocoiru. Do not post these files, generated OBS
profiles, or raw OBS/FFmpeg logs to public chats: URLs contain credentials. Sender
audio remains optional DirectShow input; empty means silence, not system audio.

## Ex and acceptance

The catalog declares `odeum-broadcast` and `odeum-obs`, both autostart=false.
After preparation and explicit startup authorization, start them through Ex on
VANMAC with Cc testing claim/release. The Ex user must be the logged-in OBS user.
Do not start from a worktree. Ex process state alone does not prove OBS UI/rendering.
In OBS select Odeum_VANMAC profile and collection; the Ex command requests those
names but deliberately has no start-streaming/start-recording arguments.

OBS **Start Recording** sends the program-return SRT output; it does not create a
local recording with this profile. **Start Streaming** sends to YouTube after its
real account/key is configured. Verify the receiving sources, reaction layer,
audio mix/sync, program return at all senders, disconnect behavior and CPU load
before an authorized unlisted YouTube trial. Real operation has not been tested.

Ex's current bootstrap manifest supports a single service identity. Odeum's existing
manifest belongs to the independent WebRTC `odeum-relay` and requires its signing-key
configuration. Do not invoke that bootstrap as an SRT installer or overwrite its
manifest. The new parent requires an existing main checkout plus the above local
preparation. Remote Ex registration alone does not install OBS/MediaMTX or expose a
remote shell. Deployment remains incomplete until preparation is performed on VANMAC.

Sources: [OBS SRT](https://obsproject.com/kb/srt-protocol-streaming-guide),
[OBS custom output implementation](https://github.com/obsproject/obs-studio/blob/32.0.4/frontend/utility/AdvancedOutput.cpp),
[MediaMTX SRT](https://mediamtx.org/docs/features/srt-specific-features),
[MediaMTX configuration](https://mediamtx.org/docs/references/configuration-file),
[MediaMTX releases](https://github.com/bluenviron/mediamtx/releases/tag/v1.21.1).
