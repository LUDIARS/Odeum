# odeum-runner: the remote test runner ported from the imported Custos copy (custos/). Every
# target here is hardened with Lorica; external input is charged against Lorica budgets.
#   odeum_runner_core      pure rules (configuration, policies, wire shapes); no I/O, no environment
#   odeum_runner_adapters  processes, HTTP client, files, OS input, logging
#   odeum_runner_app       use cases, routes, WebSocket broker, HTTP server, embedded web UI
#   odeum_runner_webrtc    WebRTC streaming (libdatachannel), with ODEUM_RUNNER_WEBRTC
add_library(odeum_runner_core STATIC
  core/json.cpp core/text.cpp core/app_config.cpp core/registry.cpp core/app_view.cpp core/auth_policy.cpp
  core/control_lease.cpp core/client_message.cpp core/audience.cpp core/stream_prefs.cpp core/web_access.cpp
  core/bridge_contract.cpp core/keys.cpp core/ffmpeg_args.cpp core/playlog_row.cpp core/unityctl.cpp
  core/farm.cpp core/farm_host_auth.cpp core/ice.cpp core/h264.cpp core/video_feedback_policy.cpp core/rtcp.cpp)
target_include_directories(odeum_runner_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(odeum_runner_core PUBLIC cxx_std_20)
target_link_libraries(odeum_runner_core PUBLIC nlohmann_json::nlohmann_json Lorica::boundary OpenSSL::Crypto)

add_library(odeum_runner_adapters STATIC
  adapters/log.cpp adapters/clock.cpp adapters/process_common.cpp adapters/process_posix.cpp adapters/process_windows.cpp
  adapters/http_client.cpp adapters/cernere_client.cpp adapters/bridge_client.cpp adapters/desktop_input.cpp adapters/adb.cpp
  adapters/media_tools.cpp adapters/playlog_files.cpp adapters/farm_store.cpp adapters/ice_provider.cpp)
target_link_libraries(odeum_runner_adapters PUBLIC odeum_runner_core odeum_boost OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

# The browser UI is the unchanged custos/public, embedded at build time (no file-system serving).
set(ODEUM_RUNNER_WEB_DIR "${PROJECT_SOURCE_DIR}/../custos/public" CACHE PATH "Browser UI served by odeum-runner")
set(ODEUM_RUNNER_WEB_FILES
  index.html audience-log.html favicon.ico favicon.svg apple-touch-icon.png
  css/styles.css css/windows.css css/pad.css css/audience.css css/gaze.css
  js/api.js js/app.js js/audience-content.js js/audience-log.js js/audience-overlay.js js/audience-panel.js
  js/comment-overlay.js js/comment-panel.js js/control-state.js js/layout-mode.js js/log-view.js
  js/play-eye-tracking.js js/play-input-logic.js js/play-input.js js/rtc-stats-logic.js js/settings-panel.js
  js/status-menu.js js/stream-view.js js/touch-guard.js js/unity-panel-logic.js js/unity-panel.js
  js/virtual-pad-logic.js js/virtual-pad.js js/webrtc.js js/window-layout-logic.js js/window-manager.js js/ws-client.js
  js/gaze/gazeCalibration.js js/gaze/gazeFeatures.js js/gaze/localFaceModel.js js/gaze/playGazeCalibration.js
  js/gaze/playTrackingLifecycle.js js/gaze/privateLiveTracking.js)
set(ODEUM_RUNNER_WEB_INPUTS ${ODEUM_RUNNER_WEB_FILES})
list(TRANSFORM ODEUM_RUNNER_WEB_INPUTS PREPEND ${ODEUM_RUNNER_WEB_DIR}/)
string(REPLACE ";" "|" ODEUM_RUNNER_WEB_FILE_ARG "${ODEUM_RUNNER_WEB_FILES}")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/runner_web_assets.cpp
  COMMAND ${CMAKE_COMMAND} -DWEB_DIR=${ODEUM_RUNNER_WEB_DIR} "-DFILES=${ODEUM_RUNNER_WEB_FILE_ARG}"
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/runner_web_assets.cpp -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_web.cmake
  DEPENDS ${ODEUM_RUNNER_WEB_INPUTS} ${CMAKE_CURRENT_SOURCE_DIR}/embed_web.cmake VERBATIM)

add_library(odeum_runner_app STATIC
  app/http_types.cpp app/http_server.cpp app/settings.cpp app/apps_runner.cpp app/input_forwarder.cpp
  app/screenshot_streamer.cpp app/playlog_recorder.cpp app/unity_proxy.cpp app/ws_broker.cpp app/api_router.cpp
  app/gaze_assets.cpp ${CMAKE_CURRENT_BINARY_DIR}/runner_web_assets.cpp)
target_link_libraries(odeum_runner_app PUBLIC odeum_runner_adapters)

set(ODEUM_RUNNER_TARGETS odeum_runner_core odeum_runner_adapters odeum_runner_app)
if(ODEUM_RUNNER_WEBRTC)
  add_library(odeum_runner_webrtc STATIC
    webrtc/rtp_source_pool.cpp webrtc/video_pipeline.cpp webrtc/audio_pipeline.cpp webrtc/webrtc_broker.cpp)
  target_link_libraries(odeum_runner_webrtc PUBLIC odeum_runner_app datachannel)
  # libdatachannel's headers are third-party code: no warnings from them in our -Werror builds.
  target_include_directories(odeum_runner_webrtc SYSTEM PRIVATE $<TARGET_PROPERTY:datachannel,INTERFACE_INCLUDE_DIRECTORIES>)
  list(APPEND ODEUM_RUNNER_TARGETS odeum_runner_webrtc)
endif()

add_executable(odeum-runner app/main.cpp)
if(ODEUM_RUNNER_WEBRTC)
  target_link_libraries(odeum-runner PRIVATE odeum_runner_webrtc)
  target_compile_definitions(odeum-runner PRIVATE ODEUM_RUNNER_HAS_WEBRTC)
else()
  target_link_libraries(odeum-runner PRIVATE odeum_runner_app)
endif()
add_executable(odeum-unityctl unityctl/main.cpp)
target_link_libraries(odeum-unityctl PRIVATE odeum_runner_adapters)
foreach(exe odeum-runner odeum-unityctl)
  set_target_properties(${exe} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
  list(APPEND ODEUM_RUNNER_TARGETS ${exe})
endforeach()
install(TARGETS odeum-runner odeum-unityctl RUNTIME DESTINATION bin)

# Warnings as errors for the runner's own code only (the other native targets keep their flags).
option(ODEUM_RUNNER_WERROR "Treat warnings in odeum-runner targets as errors" OFF)
# The server's libraries and odeum-runner take EXTENDED (long-running); odeum-unityctl and the
# tests keep the baseline.
function(odeum_runner_harden target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "EXTENDED" "" "")
  if(arg_EXTENDED)
    odeum_harden(${target} EXTENDED)
  else()
    odeum_harden(${target})
  endif()
  if(ODEUM_RUNNER_WERROR)
    target_compile_options(${target} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>)
  endif()
endfunction()
foreach(target IN LISTS ODEUM_RUNNER_TARGETS)
  if(target STREQUAL "odeum-unityctl")
    odeum_runner_harden(${target})
  else()
    odeum_runner_harden(${target} EXTENDED)
  endif()
endforeach()
