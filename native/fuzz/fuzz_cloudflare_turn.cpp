// libFuzzer entry: the Cloudflare TURN reply odeum-relay reads over HTTPS (relay/cloudflare_turn.cpp):
// the raw HTTP/1.x response, then the generate-ice-servers JSON body.
#include "cloudflare_turn.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view raw(reinterpret_cast<const char*>(data), size);
    const auto reply = odeum::relay::parse_http_reply(raw);
    for (const std::string_view body : {reply ? std::string_view(reply->body) : std::string_view(), raw}) {
        try {
            (void)odeum::relay::cloudflare_ice_servers(body);
        } catch (const std::runtime_error&) {
        }
    }
    return 0;
}
