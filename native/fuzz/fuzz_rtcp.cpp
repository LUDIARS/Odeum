// libFuzzer entry: RTCP keyframe requests from a peer (protocol/rtcp.cpp).
#include <odeum/rtcp.hpp>
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    (void)odeum::is_keyframe_request({reinterpret_cast<const std::byte*>(data), size});
    return 0;
}
