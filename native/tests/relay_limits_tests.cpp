// Boundary tests for the relay's own inputs: HTTP query parameters and the operator
// configuration (public key file, ICE servers from the environment).
#include "check.hpp"
#include "config.hpp"
#include "http_query.hpp"
#include <odeum/json_limits.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace odeum;
using namespace odeum::relay;

namespace {
std::string query(std::size_t parameters) {
    std::string target = "/v1/guest?";
    for (std::size_t i = 0; i < parameters; ++i) target += (i ? "&k" : "k") + std::to_string(i) + "=v";
    return target;
}
// A JSON object of exactly `size` bytes: {"k":"aaa..."}.
std::string object_of(std::size_t size) { return R"({"k":")" + std::string(size - 8, 'a') + "\"}"; }
template<class F> bool fails(F action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
}

int main() { return run([] {
    check(parse_target(query(max_query_parameters)).has_value(), "Parameters at the limit");
    check(!parse_target(query(max_query_parameters + 1)), "One parameter over the limit");
    check(!parse_target("/v1/guest?" + std::string(16000, '&')), "Empty parameters are malformed");
    check(parse_target("/v1/guest?code=" + std::string(16000, 'a'))->query.at("code").size() == 16000, "Long value within the header limit");

    const auto keys = std::filesystem::temp_directory_path() / ("odeum_relay_keys_" + std::to_string(::getpid()) + ".json");
    ::setenv("ODEUM_RELAY_TICKET_PUBKEYS", keys.c_str(), 1);
    ::unsetenv("ODEUM_RELAY_ICE_SERVERS");
    std::ofstream(keys, std::ios::binary | std::ios::trunc) << object_of(max_config_file_bytes);
    check(load_config().keys.at("k").get_ref<const std::string&>().size() == max_config_file_bytes - 8, "Key file at the limit");
    std::ofstream(keys, std::ios::binary | std::ios::trunc) << object_of(max_config_file_bytes + 1);
    check(fails([] { load_config(); }), "Key file one byte over the limit");
    std::ofstream(keys, std::ios::binary | std::ios::trunc) << std::string(17, '[') + std::string(17, ']');
    check(fails([] { load_config(); }), "Key file nested past the limit");
    std::ofstream(keys, std::ios::binary | std::ios::trunc) << "{\"k\":";
    check(fails([] { load_config(); }), "Broken key file");

    std::ofstream(keys, std::ios::binary | std::ios::trunc) << R"({"k":"pem"})";
    ::setenv("ODEUM_RELAY_ICE_SERVERS", R"([{"urls":["stun:stun.example:3478"]}])", 1);
    check(load_config().ice->welcome().size() == 1, "ICE servers from the environment");
    ::setenv("ODEUM_RELAY_ICE_SERVERS", (std::string(16, '[') + std::string(16, ']')).c_str(), 1);
    check(fails([] { load_config(); }), "ICE JSON within the depth limit but not servers");
    ::setenv("ODEUM_RELAY_ICE_SERVERS", (std::string(17, '[') + std::string(17, ']')).c_str(), 1);
    bool limited = false;
    try { load_config(); } catch (const ProtocolError& error) { limited = error.code == "invalid_message"; }
    check(limited, "ICE JSON nested past the limit");
    std::filesystem::remove(keys);
}); }
