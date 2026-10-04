#include <odeum/message.hpp>
#include <array>
#include <algorithm>
#include <set>

namespace odeum {
namespace {
constexpr std::array names{"welcome", "sdp", "candidate", "good", "stamp", "comment", "poll.open",
    "poll.close", "poll.closed", "poll.answer", "tally", "reaction.burst", "presence", "error"};
void require(bool condition) {
    if (!condition) throw ProtocolError("invalid_message", "Invalid message fields");
}
void count(const Json& value, std::int64_t low, std::int64_t high) {
    require(value.is_number_integer());
    if (value.is_number_unsigned()) require(value.get<std::uint64_t>() >= static_cast<std::uint64_t>(low) && value.get<std::uint64_t>() <= static_cast<std::uint64_t>(high));
    else require(value.get<std::int64_t>() >= low && value.get<std::int64_t>() <= high);
}
void identity(const Json& value) {
    require_text(value, "sub", 256); require_text(value, "name", 64, true);
}
void poll(const Json& value) {
    require_text(value, "poll_id", 128); require_text(value, "question", 1000);
    require(value.at("multi").is_boolean());
    const auto& choices = value.at("choices");
    require(choices.is_array() && choices.size() >= 2 && choices.size() <= 6);
    for (const auto& choice : choices) { require(choice.is_string()); auto n = utf8_length(choice.get_ref<const std::string&>()); require(n > 0 && n <= 60); }
}
}
std::size_t utf8_length(std::string_view text) {
    std::size_t length = 0;
    for (std::size_t i = 0; i < text.size(); ++length) {
        auto c = static_cast<unsigned char>(text[i++]);
        if (c < 0x80) continue;
        unsigned n; std::uint32_t cp;
        if (c >= 0xc2 && c <= 0xdf) { n = 1; cp = c & 31; }
        else if (c >= 0xe0 && c <= 0xef) { n = 2; cp = c & 15; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 3; cp = c & 7; }
        else throw ProtocolError("invalid_message", "Invalid UTF-8");
        const auto width = n;
        require(i + n <= text.size());
        while (n--) { auto b = static_cast<unsigned char>(text[i++]); require((b & 0xc0) == 0x80); cp = (cp << 6) | (b & 63); }
        require(cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff));
        require((width != 2 || cp >= 0x800) && (width != 3 || cp >= 0x10000));
    }
    return length;
}
void require_text(const Json& object, std::string_view key, std::size_t maximum, bool empty) {
    const auto& value = object.at(std::string(key)); require(value.is_string());
    auto n = utf8_length(value.get_ref<const std::string&>()); require(n <= maximum && (empty || n > 0));
}
Message parse_message(std::string_view wire) {
    if (wire.size() > max_message_bytes) throw ProtocolError("message_too_large", "Message exceeds 16 KiB");
    try {
        utf8_length(wire);
        auto j = Json::parse(wire); require(j.is_object()); require_text(j, "type", 32);
        const auto name = j.at("type").get<std::string>();
        auto it = std::find(names.begin(), names.end(), name);
        if (it == names.end()) throw ProtocolError("unknown_type", "Unknown message type");
        auto type = static_cast<MessageType>(it - names.begin());
        switch (type) {
        case MessageType::good: count(j.at("count"), 1, 50); break;
        case MessageType::stamp: {
            require_text(j, "kind", 16); const auto k = j.at("kind").get<std::string>();
            require(k == "clap" || k == "laugh" || k == "wow" || k == "question" || k == "agree");
            if (j.contains("from")) { identity(j.at("from")); count(j.at("at"), 0, INT64_MAX); } break;
        }
        case MessageType::comment:
            require_text(j, "text", 280, true);
            if (j.contains("from")) { identity(j.at("from")); count(j.at("at"), 0, INT64_MAX); } break;
        case MessageType::sdp:
            require_text(j.at("sdp"), "type", 6); require_text(j.at("sdp"), "sdp", max_message_bytes);
            require(j["sdp"]["type"] == "offer" || j["sdp"]["type"] == "answer"); break;
        case MessageType::candidate: require_text(j, "candidate", 4096, true); require_text(j, "mid", 64); break;
        case MessageType::poll_open: poll(j); break;
        case MessageType::poll_close: require_text(j, "poll_id", 128); break;
        case MessageType::poll_answer: {
            require_text(j, "poll_id", 128); const auto& a = j.at("choices");
            require(a.is_array() && !a.empty() && a.size() <= 6); std::set<int> unique;
            for (const auto& v : a) { count(v, 0, 5); require(unique.insert(v.get<int>()).second); } break;
        }
        case MessageType::poll_closed: require_text(j, "poll_id", 128); require(j.at("tally").is_object());
            require(j["tally"].at("counts").is_array() && j["tally"]["counts"].size() >= 2 && j["tally"]["counts"].size() <= 6);
            count(j["tally"].at("answered"), 0, INT64_MAX);
            for (const auto& v : j["tally"]["counts"]) count(v, 0, INT64_MAX); break;
        case MessageType::tally:
            require_text(j, "poll_id", 128); require(j.at("counts").is_array() && j["counts"].size() >= 2 && j["counts"].size() <= 6);
            count(j.at("answered"), 0, INT64_MAX); for (const auto& v : j["counts"]) count(v, 0, INT64_MAX); break;
        case MessageType::reaction_burst:
            count(j.at("good"), 0, INT64_MAX); require(j.at("stamps").is_object());
            for (const auto& [k, v] : j["stamps"].items()) { require(k == "clap" || k == "laugh" || k == "wow" || k == "question" || k == "agree"); count(v, 0, INT64_MAX); } break;
        case MessageType::presence: require(j.at("presenter_connected").is_boolean()); count(j.at("viewer_count"), 0, INT64_MAX); break;
        case MessageType::welcome:
            require_text(j, "sid", 256); parse_role(j.at("role").get<std::string>()); identity(j.at("self")); require(j.at("ice_servers").is_array()); break;
        case MessageType::error: require_text(j, "code", 64); require_text(j, "message", 1000); break;
        }
        return {type, std::move(j)};
    } catch (const ProtocolError&) { throw; }
    catch (const Json::exception&) { throw ProtocolError("invalid_message", "Invalid JSON message"); }
}
std::string serialize_message(const Message& message) {
    auto wire = message.body.dump(); auto parsed = parse_message(wire);
    if (parsed.type != message.type) throw ProtocolError("invalid_message", "Message type mismatch");
    return wire;
}
std::string role_name(Role r) { return r == Role::presenter ? "presenter" : r == Role::viewer ? "viewer" : "service"; }
Role parse_role(std::string_view r) {
    if (r == "presenter") return Role::presenter; if (r == "viewer") return Role::viewer; if (r == "service") return Role::service;
    throw ProtocolError("invalid_ticket", "Invalid ticket");
}
Json error_message(std::string_view code, std::string_view message) { return {{"type", "error"}, {"code", code}, {"message", message}}; }
}
