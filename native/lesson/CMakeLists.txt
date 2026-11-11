# odeum-lesson: an AI-led class on top of Odeum. The core is pure (no sockets, no clock) so the
# whole lesson flow is testable; the transport adds the HTTP/WebSocket service and embedded pages.
add_library(odeum_lesson_core
  core/text.cpp core/plot.cpp core/provisional_model.cpp core/timeline.cpp core/timeline_player.cpp
  core/activity.cpp core/assignment.cpp core/conduct.cpp core/questions.cpp core/voice.cpp
  core/classroom.cpp core/classroom_cues.cpp core/classroom_views.cpp
  core/zip.cpp core/prep_sheet.cpp core/pptx.cpp)
target_include_directories(odeum_lesson_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(odeum_lesson_core PUBLIC odeum_protocol)

set(ODEUM_LESSON_WEB ${CMAKE_CURRENT_SOURCE_DIR}/web)
# <request path>|<file>|<media type>. Text is always UTF-8; the server adds the charset.
set(ODEUM_LESSON_ASSETS
  "/lesson|${ODEUM_LESSON_WEB}/lesson.html|text/html"
  "/console|${ODEUM_LESSON_WEB}/console.html|text/html"
  "/web/lesson.css|${ODEUM_LESSON_WEB}/lesson.css|text/css"
  "/web/lesson.js|${ODEUM_LESSON_WEB}/lesson.js|text/javascript"
  "/web/console.js|${ODEUM_LESSON_WEB}/console.js|text/javascript"
  "/web/voice.js|${ODEUM_LESSON_WEB}/voice.js|text/javascript"
  # The reconnecting socket is the relay's; the lesson pages use the same one rather than a copy.
  "/web/socket.js|${CMAKE_CURRENT_SOURCE_DIR}/../relay/web/socket.js|text/javascript"
  "/courses/action-game.plot.json|${CMAKE_CURRENT_SOURCE_DIR}/courses/action-game.plot.json|application/json")
set(ODEUM_LESSON_ASSET_FILES "")
set(ODEUM_LESSON_ASSET_LIST "")
foreach(entry IN LISTS ODEUM_LESSON_ASSETS)
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 1 file)
  list(APPEND ODEUM_LESSON_ASSET_FILES ${file})
  string(APPEND ODEUM_LESSON_ASSET_LIST "${entry}\n")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/assets.list "${ODEUM_LESSON_ASSET_LIST}")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets.cpp
  COMMAND ${CMAKE_COMMAND} -DLIST=${CMAKE_CURRENT_BINARY_DIR}/assets.list -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/assets.cpp
    -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_assets.cmake
  DEPENDS ${ODEUM_LESSON_ASSET_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/embed_assets.cmake VERBATIM)
add_library(odeum_lesson_assets ${CMAKE_CURRENT_BINARY_DIR}/assets.cpp)
target_include_directories(odeum_lesson_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(odeum_lesson_service catalog.cpp school.cpp console_guard.cpp)
target_include_directories(odeum_lesson_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(odeum_lesson_service PUBLIC odeum_lesson_core odeum_lesson_assets odeum_relay_state PRIVATE OpenSSL::Crypto)

add_library(odeum_lesson_transport config.cpp server.cpp connection.cpp)
target_link_libraries(odeum_lesson_transport PUBLIC odeum_lesson_service odeum_boost Threads::Threads)

add_executable(odeum-lesson main.cpp)
target_link_libraries(odeum-lesson PRIVATE odeum_lesson_transport)
set_target_properties(odeum-lesson PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
install(TARGETS odeum-lesson RUNTIME DESTINATION bin)
