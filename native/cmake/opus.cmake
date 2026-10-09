# libopus, pinned to the v1.5.2 release commit like libdatachannel. Its CMake package is
# installed together with odeum_sender so OdeumConfig.cmake can find it.
include_guard(GLOBAL)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE ${ODEUM_BUILD_SENDER} CACHE BOOL "" FORCE)
FetchContent_Declare(opus GIT_REPOSITORY https://github.com/xiph/opus.git
  GIT_TAG ddbe48383984d56acd9e1ab6a090c54ca6b735a6)
# libopus registers its test drivers whenever BUILD_TESTING is on; they are not built here.
set(ODEUM_SAVED_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(opus)
set(BUILD_TESTING ${ODEUM_SAVED_BUILD_TESTING})
