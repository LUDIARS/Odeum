# odeum-runner: the remote test runner ported from the imported Custos copy (custos/). Every
# target here is hardened with Lorica; external input is charged against Lorica budgets.
#   odeum_runner_core      pure rules (configuration, policies, wire shapes); no I/O, no environment
add_library(odeum_runner_core STATIC
  core/json.cpp core/text.cpp core/app_config.cpp core/registry.cpp core/app_view.cpp core/auth_policy.cpp
  core/control_lease.cpp core/client_message.cpp core/audience.cpp core/stream_prefs.cpp core/web_access.cpp)
target_include_directories(odeum_runner_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(odeum_runner_core PUBLIC cxx_std_20)
target_link_libraries(odeum_runner_core PUBLIC nlohmann_json::nlohmann_json Lorica::boundary OpenSSL::Crypto)
set(ODEUM_RUNNER_TARGETS odeum_runner_core)

# Warnings as errors for the runner's own code only (the other native targets keep their flags).
option(ODEUM_RUNNER_WERROR "Treat warnings in odeum-runner targets as errors" OFF)
function(odeum_runner_harden target)
  lorica_harden_target(${target})
  if(ODEUM_RUNNER_WERROR)
    target_compile_options(${target} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>)
  endif()
endfunction()
foreach(target IN LISTS ODEUM_RUNNER_TARGETS)
  odeum_runner_harden(${target})
endforeach()
