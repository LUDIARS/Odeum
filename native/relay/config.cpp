#include "config.hpp"
#include <boost/asio/ip/address.hpp>
#include <charconv>
#include <cstdlib>
#include <fstream>

namespace odeum::relay {
namespace {
std::string env(const char* name, const char* fallback = "") {
    if (auto value = std::getenv(name)) { if (!*value) throw std::runtime_error(std::string(name) + " cannot be empty"); return value; }
    return fallback;
}
std::size_t integer(std::string_view value, std::size_t maximum) {
    std::size_t n = 0; auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), n);
    if (error != std::errc{} || end != value.data() + value.size() || n == 0 || n > maximum) throw std::runtime_error("Invalid numeric configuration");
    return n;
}
}
Config load_config() {
    Config c;
    c.bind = env("ODEUM_RELAY_BIND", "127.0.0.1"); boost::asio::ip::make_address(c.bind);
    c.port = static_cast<std::uint16_t>(integer(env("ODEUM_RELAY_PORT", "4400"), 65535));
    c.web_access = WebAccess::from_environment(c.port);
    c.max_viewers = integer(env("ODEUM_RELAY_MAX_VIEWERS", "300"), 10000);
    c.max_sessions = integer(env("ODEUM_RELAY_MAX_SESSIONS", "32"), 1024);
    auto path = env("ODEUM_RELAY_TICKET_PUBKEYS"); if (path.empty()) throw std::runtime_error("ODEUM_RELAY_TICKET_PUBKEYS is required");
    std::ifstream keys(path, std::ios::binary); if (!keys) throw std::runtime_error("Cannot read public key file"); keys >> c.keys;
    auto range = env("ODEUM_RELAY_UDP_PORT_RANGE", "1024-65535"); auto dash = range.find('-');
    if (dash == std::string::npos) throw std::runtime_error("UDP range must be begin-end");
    c.rtc.portRangeBegin = static_cast<std::uint16_t>(integer(range.substr(0, dash), 65535));
    c.rtc.portRangeEnd = static_cast<std::uint16_t>(integer(range.substr(dash + 1), 65535));
    if (c.rtc.portRangeBegin > c.rtc.portRangeEnd) throw std::runtime_error("Invalid UDP port range");
    c.public_ip = env("ODEUM_RELAY_PUBLIC_IP");
    if (!c.public_ip.empty()) boost::asio::ip::make_address(c.public_ip);
    c.ice_servers = Json::parse(env("ODEUM_RELAY_ICE_SERVERS", "[]"));
    if (!c.ice_servers.is_array() || c.ice_servers.size() > 16) throw std::runtime_error("ICE servers must be an array of at most 16 entries");
    for (const auto& server : c.ice_servers) {
        if (!server.is_object() || !server.contains("urls")) throw std::runtime_error("Invalid ICE server");
        auto urls = server.at("urls"); if (urls.is_string()) urls = Json::array({urls});
        if (!urls.is_array() || urls.empty() || urls.size() > 16) throw std::runtime_error("Invalid ICE URLs");
        for (const auto& url : urls) {
            auto text = url.get<std::string>();
            if (!(text.starts_with("stun:") || text.starts_with("turn:") || text.starts_with("turns:"))) throw std::runtime_error("Invalid ICE scheme");
            rtc::IceServer ice(text);
            if (ice.type == rtc::IceServer::Type::Turn) {
                ice.username = server.at("username").get<std::string>(); ice.password = server.at("credential").get<std::string>();
                if (ice.username.empty() || ice.password.empty()) throw std::runtime_error("TURN credentials required");
            }
            c.rtc.iceServers.push_back(std::move(ice));
        }
    }
    c.rtc.disableAutoNegotiation = true;
    return c;
}
}
