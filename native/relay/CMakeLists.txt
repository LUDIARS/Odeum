add_library(odeum_relay_state reactions.cpp poll.cpp authentication.cpp invitations.cpp admission.cpp http_query.cpp slots.cpp)
target_include_directories(odeum_relay_state PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(odeum_relay_state PUBLIC odeum_protocol PRIVATE OpenSSL::Crypto)
set(ODEUM_WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/web)
set(ODEUM_WEB_SOURCES join.html overlay.html odeum.css effects.js socket.js join.js overlay.js)
list(TRANSFORM ODEUM_WEB_SOURCES PREPEND ${ODEUM_WEB_DIR}/)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp
  COMMAND ${CMAKE_COMMAND} -DWEB_DIR=${ODEUM_WEB_DIR} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp
    -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_web.cmake
  DEPENDS ${ODEUM_WEB_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/embed_web.cmake VERBATIM)
add_library(odeum_relay_transport config.cpp web_access.cpp media_sdp.cpp media.cpp hub.cpp server.cpp connection.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp)
target_link_libraries(odeum_relay_transport PUBLIC odeum_relay_state datachannel odeum_boost Threads::Threads)
add_executable(odeum-relay main.cpp)
target_link_libraries(odeum-relay PRIVATE odeum_relay_transport)
set_target_properties(odeum-relay PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
install(TARGETS odeum-relay RUNTIME DESTINATION bin)
