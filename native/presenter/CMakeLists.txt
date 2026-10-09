# odeum-presenter: capture, encode and send the presenter's screen, draw the audience's reactions
# with Tela/Pictor. Core has no OS API and is what the tests exercise.
include(GNUInstallDirs)
add_library(odeum_presenter_core
  core/base64url.cpp core/launch_url.cpp core/reconnect_backoff.cpp core/connection_monitor.cpp
  core/overlay_state.cpp core/poll_draft.cpp core/stream_settings.cpp core/keyframe_request.cpp
  core/h264_bitstream.cpp core/video_pipeline.cpp core/pcm_framer.cpp core/event_queue.cpp
  core/presenter_settings.cpp core/presenter_options.cpp core/nv12_scaler.cpp)
target_include_directories(odeum_presenter_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/odeum-sender>)
target_link_libraries(odeum_presenter_core PUBLIC odeum_protocol Threads::Threads)

if(NOT (ODEUM_BUILD_PRESENTER OR ODEUM_BUILD_SENDER OR ODEUM_BUILD_PROGRAM))
  return()
endif()
if(NOT WIN32 AND NOT APPLE)
  message(FATAL_ERROR "odeum_sender supports Windows and macOS")
endif()
include(${PROJECT_SOURCE_DIR}/cmake/opus.cmake)

# odeum_sender: capture, encode and send over WebRTC. odeum-presenter, odeum-program (program
# encoding and the return feed) and Cocoiru (find_package(Odeum CONFIG)) link it.
if(WIN32)
  set(ODEUM_SENDER_PLATFORM_DIR windows)
  set(ODEUM_SENDER_PLATFORM platform/windows/graphics_capture_source.cpp platform/windows/media_foundation_encoder.cpp
    platform/windows/wasapi_loopback_source.cpp platform/windows/windows_text.cpp platform/windows/windows_shell.cpp)
else()
  enable_language(OBJCXX)
  set(ODEUM_SENDER_PLATFORM_DIR macos)
  set(ODEUM_SENDER_PLATFORM platform/macos/shareable_content.mm platform/macos/screen_capture_source.mm
    platform/macos/screen_capture_audio_source.mm platform/macos/video_toolbox_encoder.mm)
endif()
add_library(odeum_sender STATIC transport/relay_link.cpp transport/ice_servers.cpp transport/media_sender.cpp transport/opus_audio_encoder.cpp
  ${ODEUM_SENDER_PLATFORM})
target_link_libraries(odeum_sender PUBLIC odeum_presenter_core datachannel PRIVATE opus)
if(WIN32)
  target_compile_definitions(odeum_sender PRIVATE UNICODE _UNICODE)
  target_link_libraries(odeum_sender PUBLIC d3d11 dxgi dwmapi mfplat mfuuid wmcodecdspuuid strmiids ksuser ole32 runtimeobject shell32 user32)
else()
  set_target_properties(odeum_sender PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
  target_compile_options(odeum_sender PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  target_link_libraries(odeum_sender PUBLIC "-framework Foundation" "-framework AppKit" "-framework ScreenCaptureKit"
    "-framework CoreMedia" "-framework CoreVideo" "-framework VideoToolbox" "-framework CoreGraphics")
endif()

if(ODEUM_BUILD_SENDER)
  include(CMakePackageConfigHelpers)
  install(TARGETS odeum_sender odeum_presenter_core odeum_protocol EXPORT OdeumTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY core transport DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/odeum-sender FILES_MATCHING PATTERN "*.hpp")
  install(DIRECTORY platform/${ODEUM_SENDER_PLATFORM_DIR} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/odeum-sender/platform
    FILES_MATCHING PATTERN "*.hpp")
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/protocol/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT OdeumTargets NAMESPACE Odeum:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Odeum)
  configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/OdeumConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/OdeumConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Odeum)
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/OdeumConfigVersion.cmake VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/OdeumConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/OdeumConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Odeum)
endif()

if(NOT (ODEUM_BUILD_PRESENTER OR ODEUM_BUILD_PROGRAM))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/tela.cmake)

# The reaction overlay (good fountain, stamps, poll, telops, viewer count): the presenter's desktop
# overlay and the reaction layer odeum-program draws onto the program.
add_library(odeum_presenter_overlay app/labels.cpp app/good_fountain.cpp app/overlay_view.cpp)
target_link_libraries(odeum_presenter_overlay PUBLIC odeum_sender Tela::Core)

if(NOT ODEUM_BUILD_PRESENTER)
  return()
endif()

add_library(odeum_presenter_app app/settings_file.cpp app/presenter_controller.cpp app/panel_view.cpp app/inbox_view.cpp
  app/placement_mapping.cpp app/presenter_app.cpp)
target_link_libraries(odeum_presenter_app PUBLIC odeum_presenter_overlay)

if(WIN32)
  add_executable(odeum-presenter WIN32 platform/windows/windows_main.cpp platform/windows/windows_platform.cpp
    platform/windows/url_scheme_registration.cpp platform/windows/single_instance.cpp)
  target_compile_definitions(odeum-presenter PRIVATE UNICODE _UNICODE)
  target_link_libraries(odeum-presenter PRIVATE odeum_presenter_app ${ODEUM_TELA_ADAPTER} shell32 user32 advapi32)
else()
  add_executable(odeum-presenter MACOSX_BUNDLE platform/macos/macos_main.mm platform/macos/macos_platform.mm
    platform/macos/url_events.mm)
  set_target_properties(odeum-presenter PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER com.ludiars.odeum.presenter
    MACOSX_BUNDLE_BUNDLE_NAME "Odeum Presenter"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
  target_compile_options(odeum-presenter PRIVATE -fobjc-arc)
  target_link_libraries(odeum-presenter PRIVATE odeum_presenter_app ${ODEUM_TELA_ADAPTER} "-framework CoreServices")
endif()
set_target_properties(odeum-presenter PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
install(TARGETS odeum-presenter RUNTIME DESTINATION bin BUNDLE DESTINATION .)
