add_library(odeum_protocol message.cpp ticket.cpp slot.cpp)
target_include_directories(odeum_protocol PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(odeum_protocol PUBLIC nlohmann_json::nlohmann_json PRIVATE OpenSSL::Crypto)
