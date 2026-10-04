cmake_minimum_required(VERSION 3.24)
project(OdeumLive VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(FetchContent)
include(CTest)
# The presenter needs Tela and Pictor; leaving it off keeps the relay build and its review as is.
option(ODEUM_BUILD_PRESENTER "Build the odeum-presenter desktop app (Tela/Pictor required)" OFF)
# The presenter's OS-independent core and its tests, without Tela, Pictor or any OS media API.
option(ODEUM_BUILD_PRESENTER_CORE "Build only the presenter core and its tests" OFF)
# Older dependency releases predate CMake 4's removal of pre-3.5 policy modes.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
find_package(OpenSSL 3 REQUIRED)
find_package(Threads REQUIRED)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
# libsrtp (inside libdatachannel) otherwise registers its own CTest drivers, which are not
# built here and show up as "Not Run" failures in our ctest run.
set(LIBSRTP_TEST_APPS OFF CACHE BOOL "" FORCE)
set(NO_MEDIA OFF CACHE BOOL "" FORCE)
set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
set(USE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# v0.23.3 includes the MSVC static-library PDB fix.
FetchContent_Declare(datachannel GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
  GIT_TAG d9391849 GIT_SUBMODULES_RECURSE TRUE)
# The zip release: CMake's tar extractor stops partway through Boost's tar archives on Windows
# ("Invalid empty pathname"), leaving the headers incomplete.
FetchContent_Declare(boost URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-b2-nodocs.zip
  URL_HASH SHA256=40efbb1f38b8cf186fd7bb12809f561ae1c233bae0f75146862e9bfce77d0d42
  SOURCE_SUBDIR odeum-header-only)
FetchContent_MakeAvailable(json datachannel boost)
add_library(odeum_boost INTERFACE)
target_include_directories(odeum_boost SYSTEM INTERFACE ${boost_SOURCE_DIR})
target_compile_definitions(odeum_boost INTERFACE BOOST_ERROR_CODE_HEADER_ONLY)
if(WIN32)
  target_link_libraries(odeum_boost INTERFACE ws2_32 mswsock)
endif()
if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
add_subdirectory(protocol)
add_subdirectory(relay)
if(ODEUM_BUILD_PRESENTER OR ODEUM_BUILD_PRESENTER_CORE)
  add_subdirectory(presenter)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
