# odeum.lorica_applied.unsafe_api.<profile>.<probe>: lorica_deprecate_unsafe_apis really rejects
# the unbounded C copy and format functions in a unit built through odeum_harden, and still
# accepts bounded code. Included from lorica_applied_tests.cmake.
#
# Each probe is a real target, hardened like the shipped ones (odeum_harden, baseline and, where
# applied, EXTENDED) and left out of the default build (EXCLUDE_FROM_ALL). Its test builds it with
# Lorica's tests/run_build_probe.cmake (nothing is run):
#   accepted_bounded   must build (so a build that refuses everything cannot pass)
#   rejected_copy      must fail with Lorica's deprecation of the unbounded copy
#   rejected_format    must fail with Lorica's deprecation of the unbounded format
# A rejected probe passes only when the build fails and the compiler output names that function's
# Lorica deprecation, so an unrelated compile error cannot pass it.
#
# The probe sources are deliberately unsafe, so they live here as strings (as in Lorica's own
# tests/unsafe_api_probes.cmake) and are written into the build tree at configure time: no tracked
# C++ source contains them. Where Odeum leaves lorica_deprecate_unsafe_apis off (MSVC, macOS,
# Clang with glibc 2.40 or newer, see cmake/OdeumLorica.cmake) the tests are registered as skipped
# with that reason, never as passed.

set(_odeum_unsafe_api_probe_accepted_bounded [=[
#include <cstdio>
#include <cstring>
#include <lorica/bounded_text.hpp>
int main() {
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%d", 1);
    return static_cast<int>(lorica::copy_string(buffer, "ok").value_or(0) + std::strlen(buffer));
}
]=])
set(_odeum_unsafe_api_probe_rejected_copy [=[
#include <cstring>
int main() { char buffer[8]; return std::strcpy(buffer, "a") == nullptr; }
]=])
set(_odeum_unsafe_api_probe_rejected_format [=[
#include <cstdio>
int main() { char buffer[8]; return std::sprintf(buffer, "%d", 1); }
]=])
# The compiler's error for each rejected probe. GCC and Clang both print
# "error: '<declaration naming the function>' is deprecated: Lorica: <reason>"; [^:]* keeps the
# match inside that one message (every other line of compiler output starts with a path:line:).
set(_odeum_unsafe_api_diagnostic_rejected_copy "error: [^:]*strcpy[^:]* is deprecated: Lorica: unbounded copy")
set(_odeum_unsafe_api_diagnostic_rejected_format "error: [^:]*sprintf[^:]* is deprecated: Lorica: unbounded format")

# odeum_lorica_add_unsafe_api_probes(<Lorica source dir>)
function(odeum_lorica_add_unsafe_api_probes lorica_root)
  set(probe_script ${lorica_root}/tests/run_build_probe.cmake)
  set(directory ${CMAKE_CURRENT_BINARY_DIR}/lorica_applied/unsafe_api)
  set(probes accepted_bounded rejected_copy rejected_format)
  foreach(profile baseline extended)
    foreach(probe IN LISTS probes)
      set(name odeum.lorica_applied.unsafe_api.${profile}.${probe})
      if(NOT ODEUM_LORICA_UNSAFE_API)
        odeum_lorica_skipped_test(${name}
          "lorica_deprecate_unsafe_apis is not applied here (${ODEUM_LORICA_UNSAFE_API_REASON})")
        continue()
      endif()
      if(profile STREQUAL "extended" AND NOT ODEUM_LORICA_EXTENDED_OK)
        odeum_lorica_skipped_test(${name} "Lorica EXTENDED is not applied here (${ODEUM_LORICA_EXTENDED_REASON})")
        continue()
      endif()
      if(NOT EXISTS ${probe_script})
        message(FATAL_ERROR "Odeum: Lorica's tests/run_build_probe.cmake is missing (${probe_script})")
      endif()
      set(source ${directory}/${profile}_${probe}.cpp)
      file(CONFIGURE OUTPUT ${source} CONTENT "${_odeum_unsafe_api_probe_${probe}}" @ONLY)
      set(target odeum_lorica_unsafe_api_${profile}_${probe})
      add_executable(${target} EXCLUDE_FROM_ALL ${source})
      if(profile STREQUAL "extended")
        odeum_harden(${target} EXTENDED)
      else()
        odeum_harden(${target})
      endif()
      if(probe MATCHES "^rejected_")
        set(expect reject)
        set(diagnostic "${_odeum_unsafe_api_diagnostic_${probe}}")
      else()
        set(expect accept)
        set(diagnostic "")
      endif()
      add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DTARGET=${target} -DCONFIG=$<CONFIG> -DEXPECT=${expect} -DDIAGNOSTIC=${diagnostic}
        -P ${probe_script})
      # The probes build in the shared build tree, so they run one at a time.
      set_tests_properties(${name} PROPERTIES RESOURCE_LOCK odeum_build_tree)
    endforeach()
  endforeach()
endfunction()
