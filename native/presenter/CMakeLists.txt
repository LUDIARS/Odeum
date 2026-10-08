# odeum-presenter: capture, encode and send the presenter's screen, draw the audience's reactions
# with Tela/Pictor. Core has no OS API and is what the tests exercise.
add_library(odeum_presenter_core
  core/base64url.cpp core/launch_url.cpp core/reconnect_backoff.cpp core/connection_monitor.cpp
  core/overlay_state.cpp core/poll_draft.cpp core/stream_settings.cpp core/keyframe_request.cpp
  core/h264_bitstream.cpp core/video_pipeline.cpp core/pcm_framer.cpp core/event_queue.cpp
  core/presenter_settings.cpp core/presenter_options.cpp core/nv12_scaler.cpp)
target_include_directories(odeum_presenter_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(odeum_presenter_core PUBLIC odeum_protocol Threads::Threads)

if(NOT ODEUM_BUILD_PRESENTER)
  return()
endif()

if(WIN32)
  set(ODEUM_PRESENTER_TELA_ADAPTER Tela::Windows)
elseif(APPLE)
  set(ODEUM_PRESENTER_TELA_ADAPTER Tela::MacOS)
else()
  message(FATAL_ERROR "odeum-presenter supports Windows and macOS")
endif()

# Tela and Pictor come from LUDIARS checkouts, never from the network. Either point at a Tela
# source tree (built here with its OS adapter; pass TELA_PICTOR_INCLUDE_DIR and
# TELA_PICTOR_LIBRARY for a Pictor built for the same CPU), or install Tela and let
# find_package locate it.
set(ODEUM_TELA_SOURCE_DIR "" CACHE PATH "Local Tela checkout to build the presenter against")
if(ODEUM_TELA_SOURCE_DIR)
  if(WIN32)
    set(TELA_BUILD_WINDOWS ON CACHE BOOL "" FORCE)
  else()
    set(TELA_BUILD_MACOS ON CACHE BOOL "" FORCE)
  endif()
  # Tela's own tests are not ours to run; keep them out of this ctest.
  set(ODEUM_SAVED_BUILD_TESTING ${BUILD_TESTING})
  set(BUILD_TESTING OFF)
  add_subdirectory(${ODEUM_TELA_SOURCE_DIR} ${CMAKE_BINARY_DIR}/tela EXCLUDE_FROM_ALL)
  set(BUILD_TESTING ${ODEUM_SAVED_BUILD_TESTING})
else()
  find_package(Tela CONFIG REQUIRED)
endif()

# Opus for the optional audio track, pinned to the v1.5.2 release commit like libdatachannel.
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(opus GIT_REPOSITORY https://github.com/xiph/opus.git
  GIT_TAG ddbe48383984d56acd9e1ab6a090c54ca6b735a6)
# libopus registers its test drivers whenever BUILD_TESTING is on; they are not built here.
set(ODEUM_SAVED_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(opus)
set(BUILD_TESTING ${ODEUM_SAVED_BUILD_TESTING})

add_library(odeum_presenter_transport transport/relay_link.cpp transport/media_sender.cpp transport/opus_audio_encoder.cpp)
target_link_libraries(odeum_presenter_transport PUBLIC odeum_presenter_core datachannel PRIVATE opus)

add_library(odeum_presenter_app app/labels.cpp app/settings_file.cpp app/presenter_controller.cpp app/good_fountain.cpp
  app/overlay_view.cpp app/panel_view.cpp app/inbox_view.cpp app/placement_mapping.cpp app/presenter_app.cpp)
target_link_libraries(odeum_presenter_app PUBLIC odeum_presenter_transport Tela::Core)

if(WIN32)
  add_executable(odeum-presenter WIN32 platform/windows/windows_main.cpp platform/windows/windows_platform.cpp
    platform/windows/windows_text.cpp platform/windows/graphics_capture_source.cpp
    platform/windows/media_foundation_encoder.cpp platform/windows/wasapi_loopback_source.cpp
    platform/windows/url_scheme_registration.cpp platform/windows/single_instance.cpp)
  target_compile_definitions(odeum-presenter PRIVATE UNICODE _UNICODE)
  target_link_libraries(odeum-presenter PRIVATE odeum_presenter_app ${ODEUM_PRESENTER_TELA_ADAPTER}
    d3d11 dxgi dwmapi mfplat mfuuid wmcodecdspuuid strmiids ksuser ole32 shell32 user32 advapi32 runtimeobject)
else()
  enable_language(OBJCXX)
  add_executable(odeum-presenter MACOSX_BUNDLE platform/macos/macos_main.mm platform/macos/macos_platform.mm
    platform/macos/shareable_content.mm platform/macos/screen_capture_source.mm
    platform/macos/screen_capture_audio_source.mm platform/macos/video_toolbox_encoder.mm platform/macos/url_events.mm)
  set_target_properties(odeum-presenter PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER com.ludiars.odeum.presenter
    MACOSX_BUNDLE_BUNDLE_NAME "Odeum Presenter"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
  target_compile_options(odeum-presenter PRIVATE -fobjc-arc)
  target_link_libraries(odeum-presenter PRIVATE odeum_presenter_app ${ODEUM_PRESENTER_TELA_ADAPTER}
    "-framework AppKit" "-framework ScreenCaptureKit" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework CoreGraphics" "-framework CoreServices")
endif()
set_target_properties(odeum-presenter PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
install(TARGETS odeum-presenter RUNTIME DESTINATION bin BUNDLE DESTINATION .)
