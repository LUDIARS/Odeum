#pragma once
#include <odeum/message.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace odeum::relay {
// Cloudflare Realtime TURN: the relay trades a TURN key id + API token for short-lived ICE
// servers (generate-ice-servers) and refreshes them before they expire. OS/network free.
struct CloudflareTurn {
    std::string key_id, api_token;
    std::chrono::seconds ttl{86400};
};

inline constexpr std::string_view cloudflare_turn_host = "rtc.live.cloudflare.com";
inline constexpr std::chrono::seconds cloudflare_turn_min_ttl{600}, cloudflare_turn_max_ttl{172800};

// Both values or neither (nullopt). ttl may be null (default 86400). Throws on invalid values
// without echoing them.
std::optional<CloudflareTurn> cloudflare_turn_from(const char* key_id, const char* api_token, const char* ttl);
std::string cloudflare_request_target(const CloudflareTurn& turn);
std::string cloudflare_request_body(const CloudflareTurn& turn);
// The complete HTTP/1.0 request (no chunked replies, server closes after the response).
std::string cloudflare_http_request(const CloudflareTurn& turn);

struct HttpReply {
    int status = 0;
    std::string body;
};
inline constexpr std::size_t http_reply_limit = 65536;
// Parses a complete raw HTTP/1.x response read until the connection closed. Handles
// Content-Length, chunked and close-delimited bodies. nullopt for malformed or oversized input.
std::optional<HttpReply> parse_http_reply(std::string_view raw);
// The response's iceServers (array, or a single object from the older endpoint) in WebRTC form.
// URLs on port 53 are dropped (browsers block them). At least one TURN entry with credentials is
// required. Throws on anything else.
Json cloudflare_ice_servers(std::string_view response_body);
// Refresh at half the credential lifetime; retry failures 5 s, 10 s, ... capped at 300 s.
std::chrono::seconds cloudflare_refresh_delay(const CloudflareTurn& turn);
std::chrono::seconds cloudflare_retry_delay(unsigned failures);
}
