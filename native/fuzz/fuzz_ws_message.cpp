// libFuzzer entry: one relay WebSocket text message (protocol/message.cpp).
#include <odeum/message.hpp>
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        (void)odeum::parse_message({reinterpret_cast<const char*>(data), size});
    } catch (const odeum::ProtocolError&) {
    }
    return 0;
}
