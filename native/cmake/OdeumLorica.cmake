# Lorica on every native build unit (job cpp-lorica-20261111).
#
#   odeum_harden(<target> [EXTENDED] [EXPORTED])
#     Links Lorica::boundary (as $<BUILD_INTERFACE:...> for targets in the installed Odeum
#     package, whose consumers do not get Lorica), applies lorica_harden_target and, where the
#     toolchain supports it, lorica_deprecate_unsafe_apis. EXTENDED asks for the extended profile
#     (long-running executables and the libraries they are built from); without toolchain support
#     the target keeps the baseline and configure prints a WARNING with Lorica's reason.
#   odeum_harden_third_party(<target>...)
#     Applies lorica_harden_target (baseline) to dependencies compiled here from source
#     (libdatachannel and its bundled libraries, opus). Their sources are not changed.
#   odeum_lorica_require_coverage()
#     FATAL_ERROR when a compiled target defined in Odeum's own directories did not go through
#     odeum_harden: a new target cannot be added without Lorica by accident.
#
# macOS: Lorica's hardening supports Windows and Linux only and refuses other platforms with a
# FATAL_ERROR. odeum-program, odeum_sender and the presenter are also built for macOS, so there
# the hardening is skipped with a WARNING naming every skipped target, and
# ODEUM_LORICA_MACOS_UNHARDENED=ON has to be passed to acknowledge it (see the porting notes).
# Lorica::boundary (the checked parsers) is linked on every platform.
include_guard(GLOBAL)

option(ODEUM_LORICA_EXTENDED "Use Lorica's EXTENDED profile for odeum-relay and odeum-runner where supported" ON)
option(ODEUM_LORICA_MACOS_UNHARDENED
  "Acknowledge that Lorica's target hardening is not available on macOS and build without it" OFF)

set(ODEUM_LORICA_HARDEN TRUE)
if(APPLE)
  set(ODEUM_LORICA_HARDEN FALSE)
  if(NOT ODEUM_LORICA_MACOS_UNHARDENED)
    message(FATAL_ERROR "Lorica's target hardening supports Windows and Linux only. Building Odeum for macOS "
      "without it needs -DODEUM_LORICA_MACOS_UNHARDENED=ON; the boundary checks stay in either way.")
  endif()
  message(WARNING "Odeum: macOS build without Lorica target hardening (ODEUM_LORICA_MACOS_UNHARDENED=ON); "
    "the skipped targets are listed below")
endif()

# lorica_deprecate_unsafe_apis: GCC/Clang force-include Lorica's header and make deprecations
# errors. On MSVC it is /we4996, which also turns the CRT's own deprecations (std::getenv in the
# relay and the runner configuration) into errors; that build cannot be checked in the cloud, so
# MSVC keeps its current flags until the HQ build has been tried (reported as a human decision).
set(ODEUM_LORICA_UNSAFE_API FALSE)
set(ODEUM_LORICA_UNSAFE_API_REASON "")
if(MSVC)
  set(ODEUM_LORICA_UNSAFE_API_REASON "not applied on MSVC: /we4996 also rejects the CRT deprecations (getenv); needs the HQ build")
elseif(ODEUM_LORICA_HARDEN)
  lorica_unsafe_api_rejection_supported(ODEUM_LORICA_UNSAFE_API)
else()
  set(ODEUM_LORICA_UNSAFE_API_REASON "not applied on macOS")
endif()
if(NOT ODEUM_LORICA_UNSAFE_API)
  message(STATUS "Odeum: lorica_deprecate_unsafe_apis ${ODEUM_LORICA_UNSAFE_API_REASON}")
endif()

set(ODEUM_LORICA_EXTENDED_OK FALSE)
set(ODEUM_LORICA_EXTENDED_REASON "ODEUM_LORICA_EXTENDED is OFF")
if(ODEUM_LORICA_HARDEN AND ODEUM_LORICA_EXTENDED)
  lorica_extended_hardening_supported(ODEUM_LORICA_EXTENDED_OK)
  set(ODEUM_LORICA_EXTENDED_REASON "${ODEUM_LORICA_EXTENDED_OK_REASON}")
  if(NOT ODEUM_LORICA_EXTENDED_OK)
    message(WARNING "Odeum: Lorica EXTENDED is not available here (${ODEUM_LORICA_EXTENDED_REASON}); "
      "odeum-relay and odeum-runner keep the baseline profile")
  endif()
endif()

function(_odeum_lorica_note_skipped target)
  set_property(TARGET ${target} PROPERTY ODEUM_LORICA_SKIPPED TRUE)
  message(WARNING "Odeum: ${target} is built without Lorica target hardening (macOS)")
endfunction()

function(odeum_harden target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "EXTENDED;EXPORTED" "" "")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "odeum_harden: unknown arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(arg_EXPORTED)
    target_link_libraries(${target} PRIVATE $<BUILD_INTERFACE:Lorica::boundary>)
  else()
    target_link_libraries(${target} PRIVATE Lorica::boundary)
  endif()
  set_property(TARGET ${target} PROPERTY ODEUM_LORICA_BOUNDARY TRUE)
  if(NOT ODEUM_LORICA_HARDEN)
    _odeum_lorica_note_skipped(${target})
    return()
  endif()
  if(arg_EXTENDED AND ODEUM_LORICA_EXTENDED_OK)
    lorica_harden_target(${target} EXTENDED)
  else()
    lorica_harden_target(${target})
  endif()
  if(ODEUM_LORICA_UNSAFE_API)
    lorica_deprecate_unsafe_apis(${target})
  endif()
endfunction()

function(odeum_harden_third_party)
  foreach(target IN LISTS ARGN)
    if(NOT TARGET ${target})
      message(FATAL_ERROR "odeum_harden_third_party: no target ${target}")
    endif()
    set_property(TARGET ${target} PROPERTY ODEUM_LORICA_THIRD_PARTY TRUE)
    if(ODEUM_LORICA_HARDEN)
      lorica_harden_target(${target})
    else()
      _odeum_lorica_note_skipped(${target})
    endif()
  endforeach()
endfunction()

# Odeum's own directories: every added subdirectory under the project source tree, excluding
# build trees, FetchContent sources (*/_deps/*) and external checkouts.
function(_odeum_lorica_collect directory out)
  get_property(targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
  get_property(children DIRECTORY ${directory} PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    cmake_path(IS_PREFIX PROJECT_SOURCE_DIR "${child}" NORMALIZE own)
    cmake_path(IS_PREFIX CMAKE_BINARY_DIR "${child}" NORMALIZE generated)
    # FetchContent sources (<build>/_deps, or a FETCHCONTENT_SOURCE_DIR_* inside the tree) are not ours.
    string(FIND "${child}" "/_deps/" fetched)
    if(own AND NOT generated AND fetched EQUAL -1)
      _odeum_lorica_collect(${child} nested)
      list(APPEND targets ${nested})
    endif()
  endforeach()
  set(${out} ${targets} PARENT_SCOPE)
endfunction()

function(odeum_lorica_require_coverage)
  set(missing "")
  set(count 0)
  _odeum_lorica_collect(${PROJECT_SOURCE_DIR} targets)
  foreach(target IN LISTS targets)
    get_target_property(kind ${target} TYPE)
    if(NOT kind MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
      continue()
    endif()
    math(EXPR count "${count} + 1")
    get_target_property(boundary ${target} ODEUM_LORICA_BOUNDARY)
    get_target_property(hardened ${target} LORICA_HARDENED)
    get_target_property(skipped ${target} ODEUM_LORICA_SKIPPED)
    get_target_property(deprecated ${target} LORICA_UNSAFE_APIS_DEPRECATED)
    if(NOT boundary OR NOT (hardened OR skipped) OR (ODEUM_LORICA_UNSAFE_API AND NOT deprecated))
      list(APPEND missing ${target})
    endif()
  endforeach()
  if(missing)
    message(FATAL_ERROR "Odeum: these targets are not covered by Lorica (call odeum_harden): ${missing}")
  endif()
  message(STATUS "Odeum: Lorica covers ${count} native targets (EXTENDED: ${ODEUM_LORICA_EXTENDED_OK}, "
    "unsafe API rejection: ${ODEUM_LORICA_UNSAFE_API})")
endfunction()
