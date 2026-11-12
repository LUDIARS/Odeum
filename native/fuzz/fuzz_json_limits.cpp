// libFuzzer entry: bounded JSON parsing of operator files and environment values
// (protocol/json_limits.cpp with config_limits), as the relay reads its keys and ICE servers.
#include <odeum/json_limits.hpp>
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        (void)odeum::parse_json_within({reinterpret_cast<const char*>(data), size}, odeum::config_limits);
    } catch (const odeum::ProtocolError&) {
    } catch (const odeum::Json::exception&) {
    }
    return 0;
}
