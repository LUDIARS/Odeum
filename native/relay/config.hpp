#pragma once
#include <odeum/message.hpp>
#include <rtc/rtc.hpp>
#include "web_access.hpp"

namespace odeum::relay {
struct Config {
    std::string bind = "127.0.0.1", public_ip;
    std::uint16_t port = 4400;
    std::size_t max_viewers = 300, max_sessions = 32;
    Json keys, ice_servers = Json::array();
    rtc::Configuration rtc;
    WebAccess web_access;
};
Config load_config();
}
