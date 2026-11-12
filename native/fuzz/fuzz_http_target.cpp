// libFuzzer entry: the relay's HTTP request target (relay/http_query.cpp).
#include "http_query.hpp"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    (void)odeum::relay::parse_target({reinterpret_cast<const char*>(data), size});
    return 0;
}
