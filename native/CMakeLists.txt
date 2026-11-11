cmake_minimum_required(VERSION 3.24)
project(OdeumLive VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(FetchContent)
include(CTest)
# compile_commands.json is read by odeum.lorica_applied.compile_commands (tests/).
if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()
# Sanitizer builds for ctest: ODEUM_SANITIZE=address,undefined or thread (GCC or Clang). The flags
# are added before any dependency is configured, so libdatachannel and its threads are
# instrumented too (TSan needs every thread's accesses). Undefined behaviour stops the test.
set(ODEUM_SANITIZE "" CACHE STRING "Sanitizers for every native target: empty, address, undefined, address,undefined or thread")
# libFuzzer entries (fuzz/): Clang only, off by default. Every target is then compiled with
# fuzzer coverage and ASan + UBSan; the GCC build only replays the seeds (odeum.fuzz.*).
option(ODEUM_FUZZ "Build the libFuzzer entries in fuzz/ (Clang only)" OFF)
if(ODEUM_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "ODEUM_FUZZ needs Clang (libFuzzer) for C and C++")
  endif()
  if(ODEUM_SANITIZE AND NOT ODEUM_SANITIZE MATCHES "address")
    message(FATAL_ERROR "ODEUM_FUZZ runs with AddressSanitizer; ODEUM_SANITIZE=${ODEUM_SANITIZE} does not fit")
  endif()
  if(NOT ODEUM_SANITIZE)
    set(ODEUM_SANITIZE address,undefined)
  endif()
  add_compile_options(-fsanitize=fuzzer-no-link)
endif()
if(ODEUM_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "ODEUM_SANITIZE is for GCC and Clang builds")
  endif()
  if(NOT ODEUM_SANITIZE MATCHES "^(address|undefined|address,undefined|undefined,address|thread)$")
    message(FATAL_ERROR "ODEUM_SANITIZE must be address, undefined, address,undefined or thread, not ${ODEUM_SANITIZE}")
  endif()
  add_compile_options(-fsanitize=${ODEUM_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${ODEUM_SANITIZE})
  message(STATUS "Odeum: sanitizers ${ODEUM_SANITIZE} on every target")
endif()
# The presenter needs Tela and Pictor; leaving it off keeps the relay build and its review as is.
option(ODEUM_BUILD_PRESENTER "Build the odeum-presenter desktop app (Tela/Pictor required)" OFF)
# The presenter's OS-independent core and its tests, without Tela, Pictor or any OS media API.
option(ODEUM_BUILD_PRESENTER_CORE "Build only the presenter core and its tests" OFF)
# odeum_sender (capture, encoding and the WebRTC sender) with install rules and OdeumConfig.cmake,
# for Cocoiru's find_package(Odeum CONFIG). Needs no Tela.
option(ODEUM_BUILD_SENDER "Build and install the odeum_sender library" OFF)
# odeum-program: the parent machine's program (switching, composition, YouTube Live, return feed).
option(ODEUM_BUILD_PROGRAM "Build the odeum-program desktop app (Tela/Pictor required)" OFF)
# odeum-program's OS-independent core (layout, mixer, FLV, RTMP, settings) and its tests.
option(ODEUM_BUILD_PROGRAM_CORE "Build only the program core and its tests" OFF)
# odeum-relay. Off builds the remaining targets without libdatachannel (e.g. the runner core).
option(ODEUM_BUILD_RELAY "Build the odeum-relay live relay" ON)
# odeum-runner / odeum-unityctl: the remote test runner ported from the imported Custos copy.
# Lorica's hardening supports Windows and Linux only, so macOS leaves the runner off.
if(APPLE)
  set(ODEUM_RUNNER_DEFAULT OFF)
else()
  set(ODEUM_RUNNER_DEFAULT ON)
endif()
option(ODEUM_BUILD_RUNNER "Build the odeum-runner remote test runner and odeum-unityctl" ${ODEUM_RUNNER_DEFAULT})
# The runner's WebRTC screen streaming (libdatachannel). Off keeps the rest of the runner.
option(ODEUM_RUNNER_WEBRTC "Build odeum-runner with WebRTC screen streaming" ON)
# Older dependency releases predate CMake 4's removal of pre-3.5 policy modes.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
find_package(OpenSSL 3 REQUIRED)
find_package(Threads REQUIRED)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
# OdeumConfig.cmake finds nlohmann_json through its installed package.
set(JSON_Install ${ODEUM_BUILD_SENDER} CACHE BOOL "" FORCE)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
# libsrtp (inside libdatachannel) otherwise registers its own CTest drivers, which are not
# built here and show up as "Not Run" failures in our ctest run.
set(LIBSRTP_TEST_APPS OFF CACHE BOOL "" FORCE)
set(NO_MEDIA OFF CACHE BOOL "" FORCE)
set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
set(USE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# v0.23.3 includes the MSVC static-library PDB fix.
FetchContent_Declare(datachannel GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
  GIT_TAG d9391849 GIT_SUBMODULES_RECURSE TRUE)
# The zip release: CMake's tar extractor stops partway through Boost's tar archives on Windows
# ("Invalid empty pathname"), leaving the headers incomplete.
FetchContent_Declare(boost URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-b2-nodocs.zip
  URL_HASH SHA256=40efbb1f38b8cf186fd7bb12809f561ae1c233bae0f75146862e9bfce77d0d42
  SOURCE_SUBDIR odeum-header-only)
set(ODEUM_NEEDS_DATACHANNEL OFF)
if(ODEUM_BUILD_RELAY OR ODEUM_BUILD_PRESENTER OR ODEUM_BUILD_PRESENTER_CORE OR ODEUM_BUILD_SENDER
    OR ODEUM_BUILD_PROGRAM OR ODEUM_BUILD_PROGRAM_CORE OR (ODEUM_BUILD_RUNNER AND ODEUM_RUNNER_WEBRTC))
  set(ODEUM_NEEDS_DATACHANNEL ON)
endif()
if(ODEUM_NEEDS_DATACHANNEL)
  FetchContent_MakeAvailable(json datachannel boost)
else()
  FetchContent_MakeAvailable(json boost)
endif()
# Lorica: input budgets and target hardening for every native target (job cpp-lorica-20261111).
# Pass LORICA_SOURCE_DIR for a local checkout; otherwise the pinned commit is fetched.
set(LORICA_SOURCE_DIR "" CACHE PATH "Local Lorica checkout (empty = fetch the pinned commit)")
set(LORICA_BUILD_PROTECTION OFF CACHE BOOL "" FORCE)
set(LORICA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(LORICA_SOURCE_DIR)
  add_subdirectory(${LORICA_SOURCE_DIR} ${CMAKE_BINARY_DIR}/_lorica EXCLUDE_FROM_ALL)
else()
  FetchContent_Declare(lorica GIT_REPOSITORY https://github.com/LUDIARS/Lorica.git
    GIT_TAG 5e8281b0e60fa16f814e16eaa620715937e8618d)
  FetchContent_MakeAvailable(lorica)
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OdeumLorica.cmake)
# libdatachannel and the libraries it builds from its submodules are compiled here from source,
# so they get Lorica's baseline hardening too (their sources and CMake files are not changed).
if(ODEUM_NEEDS_DATACHANNEL)
  odeum_harden_third_party(datachannel datachannel-static juice juice-static srtp2 usrsctp)
  # Clang's UBSan includes -fsanitize=function, which stops at libjuice's bundled picohash (it calls
  # its SHA-1 update through a pointer of another function type). That is third-party C code we do
  # not change; only this one check is turned off, only for libjuice, only in Clang UBSan builds.
  if(ODEUM_SANITIZE MATCHES "undefined" AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
    foreach(target juice juice-static)
      target_compile_options(${target} PRIVATE -fno-sanitize=function)
    endforeach()
  endif()
endif()
add_library(odeum_boost INTERFACE)
target_include_directories(odeum_boost SYSTEM INTERFACE ${boost_SOURCE_DIR})
target_compile_definitions(odeum_boost INTERFACE BOOST_ERROR_CODE_HEADER_ONLY)
if(WIN32)
  target_link_libraries(odeum_boost INTERFACE ws2_32 mswsock)
endif()
if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
add_subdirectory(protocol)
if(ODEUM_BUILD_RELAY)
  add_subdirectory(relay)
endif()
# odeum-lesson: AI-led classes (plot -> timeline, prep sheet, in-class questions, assignment monitoring).
# It reuses the relay's state library (web access, invitations), so it follows ODEUM_BUILD_RELAY.
option(ODEUM_BUILD_LESSON "Build the odeum-lesson class service (needs ODEUM_BUILD_RELAY)" ${ODEUM_BUILD_RELAY})
if(ODEUM_BUILD_LESSON)
  if(NOT ODEUM_BUILD_RELAY)
    message(FATAL_ERROR "ODEUM_BUILD_LESSON needs ODEUM_BUILD_RELAY (odeum-lesson links odeum_relay_state)")
  endif()
  add_subdirectory(lesson)
endif()
if(ODEUM_BUILD_RUNNER)
  add_subdirectory(runner)
endif()
if(ODEUM_BUILD_PRESENTER OR ODEUM_BUILD_PRESENTER_CORE OR ODEUM_BUILD_SENDER OR ODEUM_BUILD_PROGRAM OR ODEUM_BUILD_PROGRAM_CORE)
  add_subdirectory(presenter)
endif()
if(ODEUM_BUILD_PROGRAM OR ODEUM_BUILD_PROGRAM_CORE)
  add_subdirectory(program)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
# Fuzz entries: seed replays under ctest (BUILD_TESTING) and, with ODEUM_FUZZ, libFuzzer builds.
add_subdirectory(fuzz)
# Every compiled target in Odeum's own directories must have gone through odeum_harden.
odeum_lorica_require_coverage()
odeum_lorica_write_expectations()
