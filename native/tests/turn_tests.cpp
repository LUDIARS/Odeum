#include "check.hpp"
#include "cloudflare_turn.hpp"
#include "ice_directory.hpp"
using namespace odeum::relay;
using odeum::Json;

template<class Function> void throws(Function action, const char* description) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(description);
}

int main() { return run([] {
    // Settings: both or neither, validated, TTL bounds.
    check(!cloudflare_turn_from(nullptr, nullptr, nullptr), "Unset means disabled");
    throws([] { cloudflare_turn_from("abc", nullptr, nullptr); }, "Key id without token rejected");
    throws([] { cloudflare_turn_from(nullptr, "token", nullptr); }, "Token without key id rejected");
    throws([] { cloudflare_turn_from(nullptr, nullptr, "3600"); }, "TTL alone rejected");
    throws([] { cloudflare_turn_from("abc/../x", "token", nullptr); }, "Path characters in key id rejected");
    throws([] { cloudflare_turn_from("abc", "tok en\r\nX: y", nullptr); }, "Header injection in token rejected");
    throws([] { cloudflare_turn_from("abc", "token", "599"); }, "TTL below minimum rejected");
    throws([] { cloudflare_turn_from("abc", "token", "172801"); }, "TTL above maximum rejected");
    throws([] { cloudflare_turn_from("abc", "token", "12x"); }, "Non-numeric TTL rejected");
    auto turn = *cloudflare_turn_from("0123abcd", "tok_en-1.2", nullptr);
    check(turn.ttl == std::chrono::seconds(86400), "Default TTL");
    check(cloudflare_turn_from("k", "t", "600")->ttl == std::chrono::seconds(600), "Minimum TTL accepted");
    check(cloudflare_request_target(turn) == "/v1/turn/keys/0123abcd/credentials/generate-ice-servers", "Request target");
    check(Json::parse(cloudflare_request_body(turn)) == Json{{"ttl", 86400}}, "Request body");
    {
        auto request = cloudflare_http_request(turn);
        check(request.starts_with("POST /v1/turn/keys/0123abcd/credentials/generate-ice-servers HTTP/1.0\r\n"), "Request line");
        check(request.find("\r\nHost: rtc.live.cloudflare.com\r\n") != std::string::npos, "Host header");
        check(request.find("\r\nConnection: close\r\n") != std::string::npos, "Server closes after the reply");
        check(request.find("\r\nContent-Length: " + std::to_string(cloudflare_request_body(turn).size()) + "\r\n\r\n") != std::string::npos, "Content length");
        check(request.ends_with("\r\n\r\n" + cloudflare_request_body(turn)), "Body follows the head");
    }

    // HTTP reply framing.
    {
        auto sized = parse_http_reply("HTTP/1.1 201 Created\r\nContent-Type: application/json\r\ncontent-length: 2\r\n\r\n{}trailing");
        check(sized && sized->status == 201 && sized->body == "{}", "Content-Length body");
        auto chunked = parse_http_reply("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2;x=y\r\nde\r\n0\r\n\r\n");
        check(chunked && chunked->status == 200 && chunked->body == "abcde", "Chunked body");
        auto closed = parse_http_reply("HTTP/1.0 401 Unauthorized\r\n\r\nno");
        check(closed && closed->status == 401 && closed->body == "no", "Close-delimited body");
        check(!parse_http_reply("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort"), "Truncated body rejected");
        check(!parse_http_reply("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n"), "Bad chunk rejected");
        check(!parse_http_reply("garbage"), "Missing head rejected");
        check(!parse_http_reply("HTTP/1.1 2x0 OK\r\n\r\n"), "Bad status rejected");
        check(!parse_http_reply(std::string(http_reply_limit + 1, 'a')), "Oversized reply rejected");
    }

    // Refresh schedule.
    check(cloudflare_refresh_delay(turn) == std::chrono::seconds(43200), "Refresh at half lifetime");
    check(cloudflare_retry_delay(1) == std::chrono::seconds(5), "First retry");
    check(cloudflare_retry_delay(2) == std::chrono::seconds(10), "Second retry");
    check(cloudflare_retry_delay(20) == std::chrono::seconds(300), "Retry capped");

    // Response parsing: port 53 dropped, credentials kept, STUN without credentials.
    auto servers = cloudflare_ice_servers(R"({"iceServers":[
        {"urls":["stun:stun.cloudflare.com:3478","stun:stun.cloudflare.com:53"]},
        {"urls":["turn:turn.cloudflare.com:3478?transport=udp","turn:turn.cloudflare.com:53?transport=udp",
                 "turns:turn.cloudflare.com:443?transport=tcp"],"username":"u1","credential":"c1"}]})");
    check(servers.size() == 2, "Two servers");
    check(servers[0].at("urls") == Json::array({"stun:stun.cloudflare.com:3478"}) && !servers[0].contains("username"), "STUN kept without port 53");
    check(servers[1].at("urls").size() == 2 && servers[1].at("username") == "u1" && servers[1].at("credential") == "c1", "TURN kept with credentials");
    auto single = cloudflare_ice_servers(R"({"iceServers":{"urls":["turn:turn.cloudflare.com:3478?transport=udp"],"username":"u","credential":"c"}})");
    check(single.size() == 1, "Older single-object response");
    throws([] { cloudflare_ice_servers("not json"); }, "Invalid JSON rejected");
    throws([] { cloudflare_ice_servers(R"({"errors":[]})"); }, "Missing iceServers rejected");
    throws([] { cloudflare_ice_servers(R"({"iceServers":[{"urls":["stun:stun.cloudflare.com:3478"]}]})"); }, "No TURN rejected");
    throws([] { cloudflare_ice_servers(R"({"iceServers":[{"urls":["turn:turn.cloudflare.com:3478"]}]})"); }, "TURN without credentials rejected");
    throws([] { cloudflare_ice_servers(R"({"iceServers":[{"urls":["turn:turn.cloudflare.com:53"],"username":"u","credential":"c"}]})"); }, "Only port 53 TURN rejected");

    // Static list validation (ODEUM_RELAY_ICE_SERVERS).
    auto fixed = parse_ice_servers(Json::parse(R"([{"urls":"stun:stun.example.test:3478"}])"));
    check(fixed.servers.size() == 1, "Static STUN parsed");
    throws([] { parse_ice_servers(Json::parse(R"([{"urls":"http://x"}])")); }, "Unknown scheme rejected");
    throws([] { parse_ice_servers(Json::parse(R"([{"urls":"turn:x.test:3478"}])")); }, "TURN without credentials rejected");
    throws([] { parse_ice_servers(Json::parse(R"([{"urls":"turn:x.test:3478","username":1,"credential":"c"}])")); }, "Non-string username rejected");
    throws([] { parse_ice_servers(Json::object()); }, "Non-array rejected");

    // Directory: static first, dynamic replaced on refresh, peer configuration keeps base settings.
    IceDirectory directory;
    directory.set_static(fixed);
    check(!directory.has_dynamic() && directory.welcome().size() == 1, "Static only before refresh");
    directory.set_dynamic(parse_ice_servers(servers));
    check(directory.has_dynamic() && directory.welcome().size() == 3, "Refreshed servers appended");
    rtc::Configuration base; base.portRangeBegin = 40000; base.portRangeEnd = 40100;
    auto config = directory.peer_configuration(base);
    check(config.portRangeBegin == 40000 && config.portRangeEnd == 40100, "Base settings kept");
    check(config.iceServers.size() == 1 + 1 + 2, "Peer servers: static + STUN + two TURN URLs");
    directory.set_dynamic(parse_ice_servers(single));
    check(directory.welcome().size() == 2 && directory.welcome()[1].at("username") == "u", "Refresh replaces the previous credentials");
}); }
