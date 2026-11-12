# Fuzz entries for the inputs Odeum reads from outside. Each entry is one LLVMFuzzerTestOneInput.
#   BUILD_TESTING: odeum_fuzz_<name>_regress replays seeds/<name> once per file under ctest
#                  (any compiler; this is what the GCC build runs).
#   ODEUM_FUZZ:    odeum_fuzz_<name> is the libFuzzer executable (Clang only, ASan + UBSan on
#                  every target, see the top-level CMakeLists.txt).
#                  Run: odeum_fuzz_<name> -max_total_time=30 <corpus dir> <source dir>/seeds/<name>
set(ODEUM_FUZZ_SEEDS ${CMAKE_CURRENT_SOURCE_DIR}/seeds)
function(odeum_fuzz_entry name source)
  set(libraries ${ARGN})
  foreach(library IN LISTS libraries)
    if(NOT TARGET ${library})
      return() # the library this entry exercises is not part of this configuration
    endif()
  endforeach()
  if(BUILD_TESTING)
    add_executable(odeum_fuzz_${name}_regress ${source} regress_main.cpp)
    target_link_libraries(odeum_fuzz_${name}_regress PRIVATE ${libraries})
    odeum_harden(odeum_fuzz_${name}_regress)
    add_test(NAME odeum.fuzz.${name} COMMAND odeum_fuzz_${name}_regress ${ODEUM_FUZZ_SEEDS}/${name})
  endif()
  if(ODEUM_FUZZ)
    add_executable(odeum_fuzz_${name} ${source})
    target_link_libraries(odeum_fuzz_${name} PRIVATE ${libraries})
    # ODEUM_FUZZ already compiles every target with ASan + UBSan and fuzzer coverage.
    target_compile_options(odeum_fuzz_${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(odeum_fuzz_${name} PRIVATE -fsanitize=fuzzer)
    odeum_harden(odeum_fuzz_${name})
  endif()
endfunction()

odeum_fuzz_entry(ws_message fuzz_ws_message.cpp odeum_protocol)
odeum_fuzz_entry(ticket fuzz_ticket.cpp odeum_protocol)
odeum_fuzz_entry(rtcp fuzz_rtcp.cpp odeum_protocol)
odeum_fuzz_entry(json_limits fuzz_json_limits.cpp odeum_protocol)
odeum_fuzz_entry(http_target fuzz_http_target.cpp odeum_relay_state)
odeum_fuzz_entry(cloudflare_turn fuzz_cloudflare_turn.cpp odeum_relay_state)
odeum_fuzz_entry(launch_url fuzz_launch_url.cpp odeum_presenter_core)
odeum_fuzz_entry(rtmp fuzz_rtmp.cpp odeum_program_core)
odeum_fuzz_entry(amf0 fuzz_amf0.cpp odeum_program_core)
odeum_fuzz_entry(archive fuzz_archive.cpp odeum_program_core)
odeum_fuzz_entry(runner_json fuzz_runner_json.cpp odeum_runner_core)
odeum_fuzz_entry(lesson_message fuzz_lesson_message.cpp odeum_lesson_service)
odeum_fuzz_entry(lesson_plot fuzz_lesson_plot.cpp odeum_lesson_service)
if(ODEUM_SANITIZE STREQUAL "thread")
  get_property(odeum_fuzz_tests DIRECTORY PROPERTY TESTS)
  if(odeum_fuzz_tests)
    set_tests_properties(${odeum_fuzz_tests} PROPERTIES ENVIRONMENT "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tests/tsan.supp")
  endif()
endif()
