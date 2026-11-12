// libFuzzer entry: an odeum:// launch link handed over by the OS (presenter/core/launch_url.cpp).
#include "core/launch_url.hpp"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    try {
        (void)odeum::presenter::parse_launch_url(text);
    } catch (const odeum::presenter::LaunchUrlError&) {
    }
    try {
        (void)odeum::presenter::ticket_expiry(text);
    } catch (const odeum::presenter::LaunchUrlError&) {
    }
    return 0;
}
