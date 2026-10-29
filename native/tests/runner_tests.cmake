# odeum-runner tests. Each executable takes the golden fixture directory as its argument.
set(ODEUM_RUNNER_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/runner)
function(odeum_runner_test name)
  add_executable(odeum_runner_${name}_tests runner_${name}_tests.cpp)
  target_link_libraries(odeum_runner_${name}_tests PRIVATE ${ARGN})
  target_include_directories(odeum_runner_${name}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  odeum_runner_harden(odeum_runner_${name}_tests)
  add_test(NAME odeum.runner.${name} COMMAND odeum_runner_${name}_tests ${ODEUM_RUNNER_FIXTURES})
endfunction()

odeum_runner_test(config odeum_runner_core)
odeum_runner_test(messages odeum_runner_core)
