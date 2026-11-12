# Native review environment {#SPEC-ODEUM-NATIVE-REVIEW}

## Scope

Odeum's `praesidium.json` registers every tracked C/C++ translation unit,
including native tests and the archived Custos encoder, except the 13 runner units listed in
[Runner, fuzz and limits units](#SPEC-ODEUM-NATIVE-REVIEW-RUNNER) (236 of 249). The registry does not
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
other registered units that include Lorica headers since #6 (`native/relay/http_query.cpp`,
`native/program/core/amf0.cpp`, `native/program/core/rtmp_chunk.cpp`) list the package too; without
it they stopped with `fatal error: 'lorica/…' file not found`. The compile commands' dependency lists
show no other previously registered unit reading a Lorica header.

## Runner, fuzz and limits units {#SPEC-ODEUM-NATIVE-REVIEW-RUNNER}

The translation units #6 added (`native/runner/**`, `native/fuzz/*`, `native/protocol/{bounded_file,json_limits,rtcp}.cpp`,
`native/tests/{runner_*,*_limits}_tests.cpp`) are registered from their compile commands in a build configured with
`ODEUM_BUILD_PRESENTER_CORE=ON` and `ODEUM_BUILD_PROGRAM_CORE=ON` (so that the presenter and program fuzz entries and
limits tests have a command too): the repository include roots of the command, one host package per third-party
include root (`odeum-json-3-11-3`, `odeum-boost-1-87-0`, `odeum-datachannel-d9391849`, `odeum-lorica-5e8281b`; every
Odeum target links `Lorica::boundary`), `odeum-openssl-3` when the unit reads OpenSSL headers (from the system include
path, so the command has no `-I` for it), the command's own definitions (`BOOST_ERROR_CODE_HEADER_ONLY`, `RTC_*`,
`ODEUM_RUNNER_HAS_WEBRTC`) and the Windows definitions above. Lorica's hardening definitions (`_GLIBCXX_ASSERTIONS`,
`_FORTIFY_SOURCE`, `_LIBCPP_HARDENING_MODE`) and the force-included `lorica/unsafe_api.hpp` are not part of an entry,
as for the Lorica check unit. `native/fuzz/regress_main.cpp` is compiled into every regress executable with that
entry's include roots; it reads only standard headers and is registered with the `ws_message` entry's command.

`native/runner/adapters/process_posix.cpp` and `process_windows.cpp` are both built on every platform and select their
backend with `#ifdef _WIN32` inside the unit, so both are analysed on the Windows host (the POSIX one is empty there).
`native/tests/relay_limits_tests.cpp` used `setenv` / `unsetenv` / `getpid` and passed a `std::filesystem::path`'s
`c_str()` as `char*`; it now keeps both backends in the unit (`_putenv_s` / `_getpid` on Windows), so it compiles
on the analysis host.

72 of the 85 units are registered. In one Revisor-equivalent run of the 75 changed entries (these 72 and the three
Lorica fixes; two workers, 60 s per compiler) all 75 passed in 240 s, against a scheduling budget of 2,340 s.
The other 13 units analyse with the same configuration but report findings inside Boost, not in Odeum code, so they
are left unregistered until Praesidium can pass host packages as system includes (Actio
`393aed37-522c-470c-aa15-c0ddbc48b1eb`, the limitation described above for `relay/server.cpp`):

| Units | Finding (Boost 1.87.0) |
| --- | --- |
| `runner/adapters/process_common.cpp`, `process_windows.cpp`, `http_client.cpp`, `runner/app/apps_runner.cpp` | `boost/asio/detail/impl/win_thread.ipp:79:20` and `:90:38`, `cplusplus.NewDelete`, on the path that creates the IOCP thread for the first `basic_waitable_timer` (Windows only) |
| `runner/app/http_server.cpp`, `tests/runner_{auth,boundary,http,media,playlog,process,unity,webrtc}_tests.cpp` | `boost/beast/zlib/detail/inflate_stream.ipp:319:21` and `:1072:21`, `deadcode.DeadStores` |

`runner/adapters/http_client.cpp` (about 75 s) and `tests/runner_http_tests.cpp` (about 64 s) also exceed the
60-second compiler limit on this host; `tests/runner_config_tests.cpp` takes 59 to 61 s and passed in the run above,
so a change to it can fail with a timeout on a slower host. A PR that changes one of the 13 unregistered units
still stops with `native.configuration-required`; registering them would turn that into the Boost finding.

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
- `odeum.lorica_applied.{baseline,extended,compile_commands}`: `test-support`, as the probes.
  `odeum.lorica_applied.binary.<exe>` (Lorica's `verify_binary.py`, registered by CMake only when python3 is found):
  `test-support` and the domain of the executable (`live-relay`, `ai-lesson`, `remote-runner` for the runner and unityctl).
- `odeum.runner.*` (11): `remote-runner`, the domain whose membership holds `native/runner/**` and `native/tests/runner_*`.
- `odeum.fuzz.<entry>` (9, the regress executables that replay `native/fuzz/seeds/<entry>`): the domain of the library the
  entry exercises (`live-protocol`, `live-relay`, `remote-runner`, `ai-lesson`) and `live-relay`, whose membership holds
  `native/fuzz/**`.
- `odeum.relay.limits` (`live-relay`), `odeum.protocol.limits` (`live-protocol`), `odeum.lesson.limits` (`ai-lesson`).
  `native/tests/{relay,protocol}_limits_tests.cpp` are only in `test-support`'s membership, so those two records also list
  `test-support`; otherwise a change to the test itself would not run it. The older relay and protocol records
  (`odeum.media`, `odeum.protocol`, ...) have the same gap and are unchanged here.
- Presenter and program tests (including `odeum.presenter.limits`, `odeum.program.limits` and the `launch_url`, `rtmp`,
  `amf0`, `archive` fuzz entries) are only built with their CMake options and are not registered here: the review
  build does not contain them, so `--no-tests=error` would fail their records.

Every CTest test of the default build (63) has exactly one record whose `-R` expression selects only that test.

The records expect `native/build` to be configured and built by the Revisor registration's
preparation cases, in this order: `configure` (`cmake -S native -B native/build -G Ninja
-DCMAKE_BUILD_TYPE=Release`) and `setup-build` (`cmake --build native/build`). Lorica is a
private repository that `configure` fetches at the pinned commit; a review host without access to it
passes a local checkout of that commit with `-DLORICA_SOURCE_DIR=<checkout>` in its registration.
