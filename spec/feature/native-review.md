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
roots cover protocol, relay, presenter, program, odeum-lesson and the Custos encoder.
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
| `odeum-lorica-5e8281b` | Lorica `include/` at commit `5e8281b0e60fa16f814e16eaa620715937e8618d`, as pinned by `native/CMakeLists.txt` |
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

## odeum-lesson units {#SPEC-ODEUM-NATIVE-REVIEW-LESSON}

The 42 odeum-lesson translation units (`native/lesson/**`, `native/tests/lesson_*_tests.cpp`)
follow the CMake targets in `native/lesson/CMakeLists.txt` rather than the relay's
all-package entry, so each unit only asks the host for headers its target links:

| Target | Units | Include roots | Packages | Extra definitions |
| --- | --- | --- | --- | --- |
| `odeum_lesson_core` | `core/*.cpp` (17) | `native/protocol/include`, `native/lesson/core` | `odeum-json-3-11-3` | none |
| `odeum_lesson_service` | `catalog`, `school`, `tool_desk`, `console_guard` | + `native/relay`, `native/lesson` | + `odeum-openssl-3` | none |
| `odeum_lesson_transport` + `odeum-lesson` | `config`, `server`, `server_tools`, `connection`, `main` | as service | + `odeum-boost-1-87-0` | `BOOST_ERROR_CODE_HEADER_ONLY` |
| lesson tests | `native/tests/lesson_*_tests.cpp` (16) | as service + `native/tests` | as service | none |

All of them carry the Windows definitions the other Odeum units use
(`WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `_WIN32_WINNT=0x0A00`, `UNICODE`, `_UNICODE`). The
libdatachannel definitions are omitted because no lesson target links libdatachannel.

The first scoped analysis of the lesson change (Windows target, clang 18) found:

- `core/timeline.cpp`: `core.DivideZero` in `build_timeline` when the sections' minutes add up
  to zero. `parse_plot` never produces such a plot, but `build_timeline` is public; it now
  rejects it with `invalid_plot`, and `lesson_timeline_tests.cpp` checks that.
- `server.cpp`, `connection.cpp`: the two `deadcode.DeadStores` warnings in Boost's
  `inflate_stream.ipp`, the same SYSTEM-include limitation described above for
  `relay/server.cpp` (Actio `393aed37-522c-470c-aa15-c0ddbc48b1eb`). They are not lesson defects
  and remain errors until Praesidium can pass host packages as system includes.

## Lorica check unit {#SPEC-ODEUM-NATIVE-REVIEW-LORICA}

`native/tests/lorica_applied_tests.cpp` is registered like its baseline target
(`odeum_lorica_applied_baseline_tests`): no repository include roots, the package
`odeum-lorica-5e8281b`, the Windows definitions above, and `ODEUM_LORICA_TEST_EXTENDED=0`,
`ODEUM_LORICA_TEST_OPTIMIZED=1`, which the unit requires (it stops with `#error` without them).
Lorica's force-included `lorica/unsafe_api.hpp` is not part of the entry (Praesidium units have no
force-include); the unsafe-API rejection is checked by the build probes, not by this unit. The
other units that include Lorica headers since #6 (`native/relay/http_query.cpp`,
`native/program/core/amf0.cpp`, `native/program/core/rtmp_chunk.cpp`) do not list the package yet,
and the runner, fuzz and new test units from #6 are not registered.

## Registered native tests {#SPEC-ODEUM-NATIVE-REVIEW-TESTS}

Because `.augur/tests.jsonl` exists, Revisor runs only the registered preparation cases
(names such as `configure`, `setup-*`, `install-*`) and then the Augur bundle of each target
domain; plain `build` / `ctest` cases are not run for code changes. The native CTest tests are
therefore ledger records (`runner: command`), one per CTest name, for example
`ctest --test-dir native/build -R ^odeum\.lesson\.plot$ --output-on-failure --no-tests=error`.
`--no-tests=error` makes a record fail when its executable was not built instead of passing empty.

- `odeum.lesson.*` (16): business domain `ai-lesson`.
- `odeum.protocol`, `odeum.ticket`: `live-protocol`. The other default-build relay tests: `live-relay`.
- `odeum.overlay` (`live-relay`) was registered as `candidate` while it crashed at process exit
  (libdatachannel threads touching OpenSSL during `atexit`, see
  `spec/tasks/2026-11-11-ai-lesson-handoff.md`). #6 fixed that; #8 checked 30 of 30 runs and promoted it to `active`.
- `odeum.lorica_applied.unsafe_api.{baseline,extended}.{accepted_bounded,rejected_copy,rejected_format}`
  (`test-support`, the domain whose membership holds `native/tests/lorica_*`, so the bundle runs
  whenever those files change): each builds one probe target of
  `native/tests/lorica_unsafe_api_probes.cmake` in `native/build` (nothing is run), so they need the
  configured tree, not only the default build. Where Odeum does not apply
  `lorica_deprecate_unsafe_apis` (or EXTENDED) CTest reports them skipped with the reason.
  The other `odeum.lorica_applied.*` tests and the runner, fuzz and `*.limits` tests from #6 are not in the ledger yet.
- Presenter and program tests are only built with their CMake options and are not registered here.

The records expect `native/build` to be configured and built by the Revisor registration's
preparation cases, in this order: `configure` (`cmake -S native -B native/build -G Ninja
-DCMAKE_BUILD_TYPE=Release`) and `setup-build` (`cmake --build native/build`). Lorica is a
private repository that `configure` fetches at the pinned commit; a review host without access to it
passes a local checkout of that commit with `-DLORICA_SOURCE_DIR=<checkout>` in its registration.
