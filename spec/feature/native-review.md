# Native review environment {#SPEC-ODEUM-NATIVE-REVIEW}

## Scope

Odeum's `praesidium.json` registers every tracked C/C++ translation unit,
including native tests and the archived Custos encoder. The registry does not
exclude optional targets or mark unavailable dependencies as successful.
Praesidium performs compiler analysis; it does not execute these programs.
Windows is the configured analysis platform. Objective-C++ macOS sources are
outside Praesidium's currently supported source extensions, not verified here.

The language standards follow `native/CMakeLists.txt` (C++20) and
`custos/native/custos-h264/CMakeLists.txt` (C++17). Repository-relative include
roots cover protocol, relay, presenter, program and the Custos encoder.
The Windows, static datachannel and header-only Boost definitions follow the
native CMake build. No warning suppression, replacement headers or analyzer
disabling defines are used.

## Host-owned dependencies

Header package locations are registered in the review host's
`praesidium.host.json`, outside the submitted checkout. Existing compiler and
other-project package mappings are preserved. Package directories are resolved
before any compiler process starts. Missing packages remain errors.

| Package key | Source |
| --- | --- |
| `odeum-json-3-11-3` | nlohmann/json v3.11.3 release, SHA-256 `d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d` |
| `odeum-boost-1-87-0` | Boost 1.87.0 b2-nodocs zip, SHA-256 `40efbb1f38b8cf186fd7bb12809f561ae1c233bae0f75146862e9bfce77d0d42` |
| `odeum-datachannel-d9391849` | libdatachannel commit prefix `d9391849`, as pinned by CMake |
| `odeum-opus-1-5-2` | Opus commit `ddbe48383984d56acd9e1ab6a090c54ca6b735a6`, as pinned by CMake |
| `odeum-openssl-3` | Installed OpenSSL 3 development headers |
| `tela` | Existing host registration for Tela public headers |
| `odeum-pictor` | Pictor public headers from the local checkout |

The libdatachannel codeload archive downloaded for the pinned commit has
SHA-256 `def5cc33133b84a4936b80cc7540250f1f72b28fe9e327500c98449a3efbf759`.
The Opus codeload archive has SHA-256
`18e9d9e26565e279753aafb6823a482ef85ea9fddbfdcdbff8fd095b12dd96b6`.
Release archive hashes for JSON and Boost are checked against the existing
CMake pins before extraction. Boost header entries are extracted and CRC-checked
with Python's zip reader because Windows tar reports unreadable archive entries.

## Browser test preparation

Revisor's existing Odeum registration retains all previous cases and adds
`install-play-dependencies`: `npm ci --ignore-scripts`, with `cwd: custos`.
This installs the lockfile's Vitest runner in a fresh review checkout without
running dependency lifecycle scripts. `.augur/tests.config.json` points to that
runner. This preparation does not launch the player, acquire a camera, or claim
that the tests passed.

## Known boundary

`native/program/render/pictor_offscreen_renderer.cpp` references
`pictor/surface/offscreen_target.h`, an API explicitly described by the existing
source as proposed and unavailable. Normal CMake builds omit that target unless
`ODEUM_PROGRAM_PICTOR_OFFSCREEN` is enabled. Praesidium requires every source
unit, so this is still an analysis prerequisite. Do not hide the source from the
scan or manufacture an API header to obtain a passing result.

## Configuration validation

The registry has 115 unique entries and no omitted tracked translation units.
The first complete Praesidium invocation reached 50 passed compiler analyses,
one compilation failure and 64 `native.analysis-budget` errors: Revisor's two
workers share a fixed 120-second budget. The one compilation failure was a
missing `RTC_ENABLE_WEBSOCKET=1` definition; adding the library's public CMake
definitions (`RTC_ENABLE_WEBSOCKET=1`, `RTC_ENABLE_MEDIA=1`) made the affected
file pass targeted compiler analysis. No gate timeout was changed.

The inspection budget issue is tracked as Actio
`296f96de-3bf4-48e1-94f8-f93366aaaf8f` (Revisor). Targeted follow-up compilation is
configuration diagnostics, not a substitute for the formal complete scan.
The Pictor offscreen source reports its real missing-header error. The relay
entry point, program app and program archive test source compile in targeted
analysis. Test executables and the Odeum service were not run.

Follow-up diagnostics attempted the remaining 65 units with the same compiler
flags and per-process timeout, outside the formal whole-repository gate. 61
passed and four remain unresolved:

- `program/render/pictor_offscreen_renderer.cpp`: missing proposed Pictor header.
- `relay/connection.cpp`: analysis did not complete successfully and produced
  no warning/error diagnostic. Syntax-only compilation subsequently passed;
  this does not establish analyzer completion.
- `relay/media.cpp`: analyzer reports a potential `_Rep` allocation leak at the
  end of `MediaRoom::link`. This is an unresolved finding, not a confirmed leak.
- `relay/server.cpp`: two `deadcode.DeadStores` warnings originate in Boost's
  `inflate_stream.ipp`. Product CMake marks Boost as SYSTEM; Praesidium currently
  supplies every host package with `-I`, so that include distinction is absent.

Thus the combined diagnostic attempts have 111 passing units, but there is no
passing complete gate. The optional-Pictor issue could not be filed through
bug-report because Actio reports zero teams for project `Od`; it remains recorded
here and in the existing mobile-play task's session notes.

The SYSTEM-include limitation is tracked as Actio
`393aed37-522c-470c-aa15-c0ddbc48b1eb` (Revisor). Both Revisor issues are separate
from this repository's configuration change; this patch does not modify the
reviewer's checks or acceptance policy.
