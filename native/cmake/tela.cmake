# Tela (and through it Pictor) for the desktop apps, from a LUDIARS checkout, never the network.
# Either point ODEUM_TELA_SOURCE_DIR at a Tela source tree (built here with its OS adapter; pass
# TELA_PICTOR_INCLUDE_DIR and TELA_PICTOR_LIBRARY for a Pictor built for the same CPU), or install
# Tela and let find_package locate it. Shared by odeum-presenter and odeum-program.
include_guard(GLOBAL)
if(WIN32)
  set(ODEUM_TELA_ADAPTER Tela::Windows CACHE INTERNAL "Tela OS adapter of the desktop apps")
elseif(APPLE)
  set(ODEUM_TELA_ADAPTER Tela::MacOS CACHE INTERNAL "Tela OS adapter of the desktop apps")
else()
  message(FATAL_ERROR "The Odeum desktop apps support Windows and macOS")
endif()
set(ODEUM_TELA_SOURCE_DIR "" CACHE PATH "Local Tela checkout to build the desktop apps against")
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
