#pragma once
// STUN / TURN configuration (custos/src/capture/ice-servers.ts). The same list goes to the browser
// and to the server-side peer connection.
#include "json.hpp"
#include <optional>
#include <string>
#include <vector>

namespace odeum::runner {
inline constexpr const char* default_stun_url = "stun:stun.cloudflare.com:3478";
inline constexpr long long turn_ttl_sec = 86'400;
inline constexpr long long turn_refresh_margin_ms = 60LL * 60 * 1000;

struct IceServer {
    std::vector<std::string> urls;
    std::optional<std::string> username, credential;
};
Json to_json(const IceServer& server);
Json to_json(const std::vector<IceServer>& servers);
// CUSTOS_STUN_URLS (comma separated; empty disables); unset = Cloudflare STUN.
std::vector<IceServer> stun_servers_from_env(const std::optional<std::string>& value);
// Cloudflare TURN response -> TURN servers; port-53 URLs (refused by browsers) are dropped.
std::vector<IceServer> parse_turn_response(const Json& body);
// One URL per entry, UDP only (stun: and turn: without transport=tcp), for the server peer.
std::vector<IceServer> flatten_for_peer(const std::vector<IceServer>& servers);
std::string encode_uri_component(std::string_view text);
}
