#pragma once
// Boundary checks of browser WebSocket messages (custos/src/ws/*-message.ts, audience/room.ts,
// handler.ts labelToId). The client is untrusted: shapes and ranges are verified, not assumed.
#include "json.hpp"
#include <optional>
#include <string>

namespace odeum::runner {
inline constexpr std::size_t comment_max_chars = 200;
inline constexpr long long comment_min_interval_ms = 1000;
inline constexpr long long audience_min_interval_ms = 750;

struct PointerMessage {
    std::string app_id;
    double x, y;
    std::string button;  // left | right | middle
    std::string phase;   // down | up | move
};
std::optional<PointerMessage> parse_pointer_message(const Json& message);

struct CommentMessage {
    std::string app_id, text;
};
std::optional<CommentMessage> parse_comment_message(const Json& message);
// Newlines and tabs become one space, other control characters are removed, runs of spaces
// collapse, and the result is trimmed. nullopt when empty or longer than 200 characters.
std::optional<std::string> normalize_comment_text(std::string_view text);
bool can_post_comment(std::optional<long long> last_at, long long now);

struct AudienceSubmission {
    std::string app_id;
    std::string kind;   // message | stamp
    std::string text;   // message: trimmed, 1..280 UTF-16 units
    std::string stamp;  // stamp: good | bad | heart | clap | party | sad | laugh
};
std::optional<AudienceSubmission> parse_audience_submission(const Json& message);

// Button label -> id: lower case, whitespace runs to '-', then only [a-z0-9-] kept.
std::string label_to_id(std::string_view label);

// Optional string field of an untrusted message; nullopt when missing or not a string.
std::optional<std::string> string_field(const Json& message, const char* key);
}
