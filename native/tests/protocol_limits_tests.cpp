// Boundary tests for the protocol's input limits: JSON budgets (bytes, values, depth), bounded
// operator files and the RTCP keyframe request reader shared by the relay and the presenter.
#include "signing.hpp"
#include <odeum/bounded_file.hpp>
#include <odeum/json_limits.hpp>
#include <odeum/rtcp.hpp>
#include <odeum/ticket.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
using namespace odeum;

namespace {
std::string nested(std::size_t depth) {
    return std::string(depth, '[') + "1" + std::string(depth, ']');
}
std::string array_of(std::size_t count) {
    std::string text = "[";
    for (std::size_t i = 0; i < count; ++i) text += i ? ",0" : "0";
    return text + "]";
}
std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> result;
    for (int v : values) result.push_back(static_cast<std::byte>(v));
    return result;
}
std::filesystem::path write_file(const std::string& name, std::size_t size) {
    const auto path = std::filesystem::temp_directory_path() / ("odeum_limits_" + std::to_string(::getpid()) + "_" + name);
    std::ofstream(path, std::ios::binary) << std::string(size, 'k');
    return path;
}
}

int main() { return run([] {
    // JSON budget: a container or primitive value or key is one element.
    constexpr JsonLimits small{64, 4, 2};
    check(parse_json_within("[1,2,3]", small).size() == 3, "Values at the limit are accepted");
    rejects([&] { parse_json_within("[1,2,3,4]", small); }, "invalid_message");
    check(parse_json_within(R"({"a":1})", JsonLimits{64, 3, 1}).at("a") == 1, "Keys count as values");
    rejects([&] { parse_json_within(R"({"a":1,"b":2})", JsonLimits{64, 3, 1}); }, "invalid_message");
    check(parse_json_within(nested(2), small).is_array(), "Depth at the limit is accepted");
    rejects([&] { parse_json_within(nested(3), small); }, "invalid_message");
    const std::string sixty_four = "\"" + std::string(62, 'a') + "\"";
    check(parse_json_within(sixty_four, small).get<std::string>().size() == 62, "Bytes at the limit are accepted");
    rejects([&] { parse_json_within(sixty_four + " ", small); }, "invalid_message");
    bool malformed = false;
    try { parse_json_within("[1,", small); } catch (const Json::parse_error&) { malformed = true; }
    check(malformed, "Malformed JSON stays a parse error");

    // The relay's WebSocket message limits through parse_message.
    check(ws_message_limits.depth == 8 && ws_message_limits.elements == 1024, "WebSocket limits");
    const auto deep = [](std::size_t depth) {
        return R"({"type":"error","code":"c","message":"m","x":)" + nested(depth - 1) + "}";
    };
    parse_message(deep(8));
    rejects([&] { parse_message(deep(9)); }, "invalid_message");
    // 1024 elements: the object, 4 keys, 3 strings, the array and 1015 numbers.
    const auto wide = [](std::size_t numbers) {
        return R"({"type":"error","code":"c","message":"m","x":)" + array_of(numbers) + "}";
    };
    parse_message(wide(1015));
    rejects([&] { parse_message(wide(1016)); }, "invalid_message");
    rejects([] { parse_message(std::string(max_message_bytes + 1, ' ')); }, "message_too_large");
    rejects([] { parse_message(std::string(max_message_bytes, '[')); }, "invalid_message");
    // The largest legal welcome (16 ICE servers of 16 URLs with TURN credentials) fits.
    Json servers = Json::array();
    for (int s = 0; s < 16; ++s) {
        Json urls = Json::array();
        for (int u = 0; u < 16; ++u) urls.push_back("turn:t" + std::to_string(u) + ".example:3478");
        servers.push_back({{"urls", urls}, {"username", "user"}, {"credential", "pass"}});
    }
    const Json welcome{{"type", "welcome"}, {"sid", "s"}, {"role", "viewer"}, {"self", {{"sub", "u"}, {"name", "n"}}},
        {"ice_servers", servers}};
    check(welcome.dump().size() <= max_message_bytes, "Welcome fits the frame limit");
    parse_message(welcome.dump());

    // Ticket parts are parsed within ticket_part_limits (depth 4, 256 values). The claims below are
    // the object, 9 keys, 8 values and the array: 19 elements before the array's numbers.
    SigningKey key;
    const TicketVerifier verifier(Json{{"key", key.pem()}});
    Json claims{{"iss","glab"},{"aud","odeum-relay"},{"sub","u"},{"name","n"},{"role","viewer"},{"sid","s"},{"exp",1100},{"jti","j"}};
    claims["x"] = Json::parse(nested(3));
    check(verifier.verify(key.sign(claims), 1000).sid == "s", "Claims at the depth limit");
    claims["x"] = Json::parse(nested(4));
    rejects([&] { verifier.verify(key.sign(claims), 1000); }, "invalid_ticket");
    claims["x"] = Json::parse(array_of(256 - 19));
    check(verifier.verify(key.sign(claims), 1000).sid == "s", "Claims at the value limit");
    claims["x"] = Json::parse(array_of(256 - 18));
    rejects([&] { verifier.verify(key.sign(claims), 1000); }, "invalid_ticket");
    rejects([&] { verifier.verify(std::string(8193, 'a'), 1000); }, "invalid_ticket");

    // Operator files: exactly the maximum is read, one byte more is refused.
    const auto exact = write_file("exact", 1000), over = write_file("over", 1001), large = write_file("large", 70000);
    check(read_file_within(exact, 1000)->size() == 1000, "File at the limit is read");
    bool refused = false;
    try { read_file_within(over, 1000); } catch (const FileTooLarge&) { refused = true; }
    check(refused, "File over the limit is refused");
    refused = false;
    try { read_file_within(large, 65536); } catch (const FileTooLarge&) { refused = true; }
    check(refused, "Large file is refused across read chunks");
    check(read_file_within(large, 70000)->size() == 70000, "Multi-chunk file at the limit is read");
    check(!read_file_within(exact.string() + ".missing", 1000), "Missing file is nullopt");
    for (const auto& path : {exact, over, large}) std::filesystem::remove(path);

    // RTCP keyframe requests.
    const auto pli = bytes({0x81, 206, 0, 2, 0, 0, 0, 1, 0x12, 0x34, 0x56, 0x78});
    const auto fir = bytes({0x84, 206, 0, 4, 0, 0, 0, 1, 0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78, 1, 0, 0, 0});
    const auto report = bytes({0x80, 201, 0, 1, 0, 0, 0, 1});
    check(is_keyframe_request(pli) && is_keyframe_request(fir), "PLI and FIR");
    check(!is_keyframe_request(report), "Receiver report is not a request");
    auto compound = report; compound.insert(compound.end(), pli.begin(), pli.end());
    check(is_keyframe_request(compound), "PLI after a receiver report");
    auto short_pli = pli; short_pli.pop_back();
    check(!is_keyframe_request(short_pli), "Length past the end");
    auto bad_version = report; bad_version.insert(bad_version.end(), pli.begin(), pli.end()); bad_version[8] = std::byte{0x41};
    check(!is_keyframe_request(bad_version), "Second packet with the wrong version");
    check(!is_keyframe_request({}), "Empty packet");
    check(!is_keyframe_request(bytes({0x80, 96, 0, 1})), "RTP media is not RTCP");
    // The largest datagram is still read; one byte more is refused before any length is followed.
    std::vector<std::byte> largest(max_rtp_packet_bytes, std::byte{0});
    std::copy(report.begin(), report.end(), largest.begin());
    largest[2] = std::byte{0x3f}; largest[3] = std::byte{0xff}; // 16384 words = 65536 bytes: past the end
    check(!is_keyframe_request(largest), "Length past a full datagram");
    auto oversized = pli; oversized.resize(max_rtp_packet_bytes + 1, std::byte{0});
    check(!is_keyframe_request(oversized), "Oversized packet");
    auto at_limit = pli; at_limit.resize(max_rtp_packet_bytes, std::byte{0});
    check(is_keyframe_request(at_limit), "PLI in a full-size datagram");
}); }
