#include "client_message.hpp"
#include "text.hpp"
#include <cmath>

namespace odeum::runner {
std::optional<std::string> string_field(const Json& message, const char* key) {
    if (!message.is_object()) return std::nullopt;
    auto it = message.find(key);
    if (it == message.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
}

namespace {
std::optional<double> unit_number(const Json& message, const char* key) {
    auto it = message.find(key);
    if (it == message.end() || !it->is_number()) return std::nullopt;
    const double v = it->get<double>();
    if (!std::isfinite(v) || v < 0 || v > 1) return std::nullopt;
    return v;
}

bool is_one_of(const std::string& value, std::initializer_list<const char*> options) {
    for (const auto* option : options) if (value == option) return true;
    return false;
}
}

std::optional<PointerMessage> parse_pointer_message(const Json& message) {
    if (string_field(message, "type") != "pointer") return std::nullopt;
    auto app_id = string_field(message, "appId");
    auto x = unit_number(message, "x"), y = unit_number(message, "y");
    auto button = string_field(message, "button"), phase = string_field(message, "phase");
    if (!app_id || app_id->empty() || !x || !y || !button || !phase) return std::nullopt;
    if (!is_one_of(*button, {"left", "right", "middle"}) || !is_one_of(*phase, {"down", "up", "move"})) return std::nullopt;
    return PointerMessage{*app_id, *x, *y, *button, *phase};
}

std::optional<CommentMessage> parse_comment_message(const Json& message) {
    if (string_field(message, "type") != "comment") return std::nullopt;
    auto app_id = string_field(message, "appId");
    auto text = string_field(message, "text");
    if (!app_id || app_id->empty() || !text) return std::nullopt;
    return CommentMessage{*app_id, *text};
}

std::optional<std::string> normalize_comment_text(std::string_view text) {
    std::vector<char32_t> cleaned;
    bool in_break = false;
    for (char32_t c : code_points(text)) {
        if (c == '\r' || c == '\n' || c == '\t') {
            if (!in_break) cleaned.push_back(' ');
            in_break = true;
            continue;
        }
        in_break = false;
        if (c <= 0x1F || c == 0x7F) continue;
        cleaned.push_back(c);
    }
    std::vector<char32_t> collapsed;
    for (char32_t c : cleaned) {
        if (c == ' ' && !collapsed.empty() && collapsed.back() == ' ') continue;
        collapsed.push_back(c);
    }
    auto result = js_trim(encode_utf8(collapsed));
    if (result.empty() || code_point_length(result) > comment_max_chars) return std::nullopt;
    return result;
}

bool can_post_comment(std::optional<long long> last_at, long long now) {
    return !last_at || now - *last_at >= comment_min_interval_ms;
}

std::optional<AudienceSubmission> parse_audience_submission(const Json& message) {
    if (!message.is_object()) return std::nullopt;
    if (string_field(message, "type") != "audience-send") return std::nullopt;
    auto app_id = string_field(message, "appId");
    if (!app_id || app_id->empty() || utf16_length(*app_id) > 200) return std::nullopt;
    auto kind = string_field(message, "kind");
    if (!kind) return std::nullopt;
    // zod .strict(): no keys beyond the envelope and the variant's own field.
    for (const auto& [key, value] : message.items()) {
        if (key == "type" || key == "appId" || key == "kind") continue;
        if (*kind == "message" && key == "text") continue;
        if (*kind == "stamp" && key == "stamp") continue;
        return std::nullopt;
    }
    if (*kind == "message") {
        auto text = string_field(message, "text");
        if (!text) return std::nullopt;
        auto trimmed = js_trim(*text);
        const auto length = utf16_length(trimmed);
        if (length < 1 || length > 280) return std::nullopt;
        return AudienceSubmission{*app_id, "message", trimmed, {}};
    }
    if (*kind == "stamp") {
        auto stamp = string_field(message, "stamp");
        if (!stamp || !is_one_of(*stamp, {"good", "bad", "heart", "clap", "party", "sad", "laugh"})) return std::nullopt;
        return AudienceSubmission{*app_id, "stamp", {}, *stamp};
    }
    return std::nullopt;
}

std::string label_to_id(std::string_view label) {
    std::string dashed;
    bool in_space = false;
    for (char32_t c : code_points(label)) {
        if (is_js_whitespace(c)) {
            if (!in_space) dashed.push_back('-');
            in_space = true;
            continue;
        }
        in_space = false;
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') dashed.push_back(static_cast<char>(c));
        else dashed.push_back('\x01');  // marks a removed character after the whitespace pass
    }
    std::string id;
    for (char c : dashed) if (c != '\x01') id.push_back(c);
    return id;
}
}
