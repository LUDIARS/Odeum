# odeum-program: receives input1..input4 from the relay, switches and composes them with the
# audience's reactions, publishes the programme to YouTube Live (FLV over RTMPS) and sends it back
# to the relay's program slot. Core has no OS API and is what the tests exercise.
add_library(odeum_program_core core/program_state.cpp core/layout.cpp core/audio_mixer.cpp core/amf0.cpp core/flv.cpp
  core/flv_feed.cpp core/rtmp_handshake.cpp core/rtmp_chunk.cpp core/rtmp_publisher.cpp core/publish_supervisor.cpp
  core/program_settings.cpp core/producer_routing.cpp)
target_include_directories(odeum_program_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(odeum_program_core PUBLIC odeum_presenter_core)

if(NOT ODEUM_BUILD_PROGRAM)
  return()
endif()

# The Pictor GPU compositor needs Pictor's offscreen render target + NV12 readback, which Pictor
# does not have yet (proposed separately); until then the CPU compositor is used.
option(ODEUM_PROGRAM_PICTOR_OFFSCREEN "Compose the programme with Pictor's offscreen target (Pictor API required)" OFF)

add_library(odeum_program_render render/nv12_canvas.cpp render/program_renderer.cpp render/layer_renderer.cpp)
target_link_libraries(odeum_program_render PUBLIC odeum_program_core odeum_presenter_overlay Tela::Pictor)
if(ODEUM_PROGRAM_PICTOR_OFFSCREEN)
  target_sources(odeum_program_render PRIVATE render/pictor_offscreen_renderer.cpp)
  target_compile_definitions(odeum_program_render PRIVATE ODEUM_PROGRAM_PICTOR_OFFSCREEN)
  target_include_directories(odeum_program_render PRIVATE ${TELA_PICTOR_INCLUDE_DIR})
  target_link_libraries(odeum_program_render PRIVATE ${TELA_PICTOR_LIBRARY})
endif()

add_library(odeum_program_transport transport/input_receiver.cpp transport/opus_audio_decoder.cpp transport/tls_connection.cpp
  transport/youtube_publisher.cpp)
target_link_libraries(odeum_program_transport PUBLIC odeum_program_core odeum_sender PRIVATE opus OpenSSL::SSL OpenSSL::Crypto)
if(WIN32)
  target_link_libraries(odeum_program_transport PRIVATE ws2_32)
endif()

add_library(odeum_program_app app/input_pipeline.cpp app/program_engine.cpp app/program_controller.cpp app/program_labels.cpp
  app/program_panel.cpp app/program_settings_file.cpp app/program_app.cpp)
target_link_libraries(odeum_program_app PUBLIC odeum_program_render odeum_program_transport)

if(WIN32)
  add_executable(odeum-program WIN32 platform/windows/windows_main.cpp platform/windows/windows_program_platform.cpp
    platform/windows/media_foundation_decoder.cpp platform/windows/media_foundation_aac_encoder.cpp
    platform/windows/dpapi_secret_store.cpp)
  target_compile_definitions(odeum-program PRIVATE UNICODE _UNICODE)
  target_link_libraries(odeum-program PRIVATE odeum_program_app ${ODEUM_TELA_ADAPTER} mfplat mfuuid wmcodecdspuuid crypt32 ole32)
else()
  enable_language(OBJCXX)
  add_executable(odeum-program platform/macos/macos_main.mm platform/macos/macos_program_platform.mm
    platform/macos/video_toolbox_decoder.mm platform/macos/audio_toolbox_aac_encoder.mm platform/macos/keychain_secret_store.mm)
  set_target_properties(odeum-program PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
  target_compile_options(odeum-program PRIVATE -fobjc-arc)
  target_link_libraries(odeum-program PRIVATE odeum_program_app ${ODEUM_TELA_ADAPTER} "-framework AppKit" "-framework VideoToolbox"
    "-framework CoreMedia" "-framework CoreVideo" "-framework AudioToolbox" "-framework Security")
endif()
set_target_properties(odeum-program PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
install(TARGETS odeum-program RUNTIME DESTINATION bin)
