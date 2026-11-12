// libFuzzer entry: an AMF0 command or data payload from the RTMP ingest server
// (program/core/amf0.cpp).
#include "core/amf0.hpp"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        (void)odeum::program::amf0_decode({reinterpret_cast<const std::byte*>(data), size});
    } catch (const odeum::program::Amf0Error&) {
    }
    return 0;
}
