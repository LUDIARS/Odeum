#include "config.hpp"
#include <odeum/slot.hpp>
#include <odeum/bounded_file.hpp>
#include <odeum/json_limits.hpp>
#include <boost/asio/ip/address.hpp>
#include <charconv>
#include <cstdlib>

namespace odeum::relay {
namespace {
std::string env(const char* name, const char* fallback = "") {
    if (auto value = std::getenv(name)) { if (!*value) throw std::runtime_error(std::string(name) + " cannot be empty"); return value; }
    return fallback;
}
// Unset is nullptr; set-but-empty is rejected like every other relay variable.
const char* optional_env(const char* name) {
    auto value = std::getenv(name);
    if (value && !*value) throw std::runtime_error(std::string(name) + " cannot be empty");
    return value;
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
    c.max_inputs = integer(env("ODEUM_RELAY_MAX_INPUTS", "4"), max_input_slots);
    auto path = env("ODEUM_RELAY_TICKET_PUBKEYS"); if (path.empty()) throw std::runtime_error("ODEUM_RELAY_TICKET_PUBKEYS is required");
    const auto keys = read_file_within(path, max_config_file_bytes); if (!keys) throw std::runtime_error("Cannot read public key file");
    c.keys = parse_json_within(*keys, config_limits);
    auto range = env("ODEUM_RELAY_UDP_PORT_RANGE", "1024-65535"); auto dash = range.find('-');
    if (dash == std::string::npos) throw std::runtime_error("UDP range must be begin-end");
    c.rtc.portRangeBegin = static_cast<std::uint16_t>(integer(range.substr(0, dash), 65535));
    c.rtc.portRangeEnd = static_cast<std::uint16_t>(integer(range.substr(dash + 1), 65535));
    if (c.rtc.portRangeBegin > c.rtc.portRangeEnd) throw std::runtime_error("Invalid UDP port range");
    c.public_ip = env("ODEUM_RELAY_PUBLIC_IP");
    if (!c.public_ip.empty()) boost::asio::ip::make_address(c.public_ip);
    c.ice->set_static(parse_ice_servers(parse_json_within(env("ODEUM_RELAY_ICE_SERVERS", "[]"), config_limits)));
    c.cloudflare = cloudflare_turn_from(optional_env("ODEUM_RELAY_CLOUDFLARE_TURN_KEY_ID"),
        optional_env("ODEUM_RELAY_CLOUDFLARE_TURN_API_TOKEN"), optional_env("ODEUM_RELAY_CLOUDFLARE_TURN_TTL"));
    c.rtc.disableAutoNegotiation = true;
    return c;
}
}
