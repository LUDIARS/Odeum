#pragma once
#include <odeum/message.hpp>
#include <rtc/rtc.hpp>
#include "web_access.hpp"

namespace odeum::relay {
struct Config {
    std::string bind = "127.0.0.1", public_ip;
    std::uint16_t port = 4400;
    // max_inputs: input slots per room (input1..inputN), 1..odeum::max_input_slots.
    std::size_t max_viewers = 300, max_sessions = 32, max_inputs = 4;
    Json keys, ice_servers = Json::array();
    rtc::Configuration rtc;
    WebAccess web_access;
};
Config load_config();
}
