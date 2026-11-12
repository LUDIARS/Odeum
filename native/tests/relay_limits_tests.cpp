// Boundary tests for the relay's own inputs: HTTP query parameters and the operator
// configuration (public key file, ICE servers from the environment).
#include "check.hpp"
#include "cloudflare_turn.hpp"
#include "config.hpp"
#include "ice_directory.hpp"
#include "http_query.hpp"
#include <odeum/json_limits.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
using namespace odeum;
using namespace odeum::relay;

namespace {
// The relay reads its configuration from the environment; both backends stay in this unit.
void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
    ::_putenv_s(name, value.c_str());
#else
    ::setenv(name, value.c_str(), 1);
#endif
}
void unset_env(const char* name) {
#ifdef _WIN32
    ::_putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}
int process_id() {
#ifdef _WIN32
    return ::_getpid();
#else
    return static_cast<int>(::getpid());
#endif
}

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
// A generate-ice-servers reply: `entries` TURN entries of `urls` URLs each, then an extra member.
std::string cloudflare_reply(std::size_t entries, std::size_t urls, const std::string& extra = "") {
    Json list = Json::array();
    for (std::size_t e = 0; e < entries; ++e) {
        Json u = Json::array();
        for (std::size_t i = 0; i < urls; ++i) u.push_back("turn:t" + std::to_string(i) + ".example:3478");
        list.push_back({{"urls", u}, {"username", "user"}, {"credential", "s3cr3t-credential"}});
    }
    auto text = Json{{"iceServers", list}}.dump();
    if (!extra.empty()) text.insert(text.size() - 1, "," + extra);
    return text;
}
// The error text of a refused reply, which must never quote the credential.
std::string refusal(const std::string& body) {
    try { cloudflare_ice_servers(body); } catch (const std::exception& error) { return error.what(); }
    throw std::runtime_error("Expected the Cloudflare reply to be refused");
}
std::string numbers(std::size_t count) {
    std::string text = "[";
    for (std::size_t i = 0; i < count; ++i) text += i ? ",0" : "0";
    return text + "]";
}
void cloudflare_limits() {
    // Entry and URL counts: the same 16 / 16 parse_ice_servers allows for the static list.
    check(parse_ice_servers(cloudflare_ice_servers(cloudflare_reply(max_cloudflare_ice_entries, max_cloudflare_ice_urls))).servers.size() == 256,
          "Cloudflare reply at the entry and URL limits");
    check(refusal(cloudflare_reply(max_cloudflare_ice_entries + 1, 1)).find("s3cr3t") == std::string::npos, "One entry over the limit");
    check(refusal(cloudflare_reply(1, max_cloudflare_ice_urls + 1)).find("s3cr3t") == std::string::npos, "One URL over the limit");
    // Depth 6: the root object plus five nested arrays under "x" (the reply itself uses 4 levels).
    cloudflare_ice_servers(cloudflare_reply(1, 1, R"("x":[[[[[1]]]]])"));
    check(refusal(cloudflare_reply(1, 1, R"("x":[[[[[[1]]]]]])")) == "Cloudflare TURN response exceeds its limits", "Nested one level over");
    // Values: the reply with one entry of one URL is 11 values; "x" and its array add 2.
    cloudflare_ice_servers(cloudflare_reply(1, 1, "\"x\":" + numbers(cloudflare_reply_limits.elements - 13)));
    check(refusal(cloudflare_reply(1, 1, "\"x\":" + numbers(cloudflare_reply_limits.elements - 12))) == "Cloudflare TURN response exceeds its limits",
          "One value over the limit");
    // Bytes: the whole body within the HTTP reply limit.
    const auto base = cloudflare_reply(1, 1, R"("pad":"")");
    const auto padded = [&](std::size_t size) { auto t = base; t.insert(t.size() - 2, std::string(size - base.size(), ' ')); return t; };
    cloudflare_ice_servers(padded(http_reply_limit));
    check(refusal(padded(http_reply_limit + 1)) == "Cloudflare TURN response exceeds its limits", "Body one byte over");
    // Broken and huge input; no message quotes the body.
    const auto broken = cloudflare_reply(1, 1).substr(0, 60);
    check(refusal(broken) == "Unexpected Cloudflare TURN response", "Broken JSON");
    check(refusal(std::string(1u << 20, '[')).find("s3cr3t") == std::string::npos, "Huge input");
    check(!parse_http_reply("HTTP/1.1 200 OK\r\n\r\n" + std::string(http_reply_limit, 'a')), "HTTP reply over the limit");
}
}

int main() { return run([] {
    check(parse_target(query(max_query_parameters)).has_value(), "Parameters at the limit");
    check(!parse_target(query(max_query_parameters + 1)), "One parameter over the limit");
    check(!parse_target("/v1/guest?" + std::string(16000, '&')), "Empty parameters are malformed");
    check(parse_target("/v1/guest?code=" + std::string(16000, 'a'))->query.at("code").size() == 16000, "Long value within the header limit");

    const auto keys = std::filesystem::temp_directory_path() / ("odeum_relay_keys_" + std::to_string(process_id()) + ".json");
    set_env("ODEUM_RELAY_TICKET_PUBKEYS", keys.string());
    unset_env("ODEUM_RELAY_ICE_SERVERS");
    std::ofstream(keys, std::ios::binary | std::ios::trunc) << object_of(max_config_file_bytes);
    check(load_config().keys.at("k").get_ref<const std::string&>().size() == max_config_file_bytes - 8, "Key file at the limit");
    std::ofstream(keys, std::ios::binary | std::ios::trunc) << object_of(max_config_file_bytes + 1);
    check(fails([] { load_config(); }), "Key file one byte over the limit");
    std::ofstream(keys, std::ios::binary | std::ios::trunc) << std::string(17, '[') + std::string(17, ']');
    check(fails([] { load_config(); }), "Key file nested past the limit");
    std::ofstream(keys, std::ios::binary | std::ios::trunc) << "{\"k\":";
    check(fails([] { load_config(); }), "Broken key file");

    std::ofstream(keys, std::ios::binary | std::ios::trunc) << R"({"k":"pem"})";
    set_env("ODEUM_RELAY_ICE_SERVERS", R"([{"urls":["stun:stun.example:3478"]}])");
    check(load_config().ice->welcome().size() == 1, "ICE servers from the environment");
    set_env("ODEUM_RELAY_ICE_SERVERS", std::string(16, '[') + std::string(16, ']'));
    check(fails([] { load_config(); }), "ICE JSON within the depth limit but not servers");
    set_env("ODEUM_RELAY_ICE_SERVERS", std::string(17, '[') + std::string(17, ']'));
    bool limited = false;
    try { load_config(); } catch (const ProtocolError& error) { limited = error.code == "invalid_message"; }
    check(limited, "ICE JSON nested past the limit");
    std::filesystem::remove(keys);
    cloudflare_limits();
}); }
