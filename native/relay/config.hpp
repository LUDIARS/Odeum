#pragma once
#include <odeum/message.hpp>
#include <rtc/rtc.hpp>
#include "cloudflare_turn.hpp"
#include "ice_directory.hpp"
#include "web_access.hpp"
#include <memory>
#include <optional>

namespace odeum::relay {
struct Config {
    std::string bind = "127.0.0.1", public_ip;
    std::uint16_t port = 4400;
    // max_inputs: input slots per room (input1..inputN), 1..odeum::max_input_slots.
    std::size_t max_viewers = 300, max_sessions = 32, max_inputs = 4;
    Json keys;
    // Base peer-connection settings (port range etc.); ICE servers come from `ice` per connection.
    rtc::Configuration rtc;
    // ODEUM_RELAY_ICE_SERVERS plus refreshed Cloudflare TURN credentials when `cloudflare` is set.
    std::shared_ptr<IceDirectory> ice = std::make_shared<IceDirectory>();
    std::optional<CloudflareTurn> cloudflare;
    WebAccess web_access;
};
Config load_config();
}
