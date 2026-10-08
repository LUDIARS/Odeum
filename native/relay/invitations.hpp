#pragma once
#include <odeum/message.hpp>
#include <chrono>
#include <deque>
#include <map>
#include <optional>
#include <string>

namespace odeum::relay {
using Millis = std::int64_t;
// Capability lookups for guest join codes and program overlay keys. GLab keeps the plaintext;
// presenter tickets carry only base64url SHA-256 digests, registered here while the room is live.
class Invitations {
public:
    static constexpr std::size_t max_failures_per_minute = 30;
    // Throws invite_conflict when a digest already belongs to another live session.
    void attach(const std::string& sid, const std::string& join_digest, const std::string& overlay_digest);
    void detach(const std::string& sid);
    // Returns the live session id. Unknown or malformed values throw session_not_live / rate_limited.
    std::string resolve_join(std::string_view code, Millis now);
    std::string resolve_overlay(std::string_view key, Millis now);
private:
    struct Entry { std::string join, overlay; };
    std::map<std::string, Entry> sessions_;
    std::map<std::string, std::string> joins_, overlays_;
    std::deque<Millis> failures_;
    std::string resolve(std::map<std::string, std::string>& index, const std::optional<std::string>& digest, Millis now);
};
// Crockford base32 normalisation: uppercase, O->0, I/L->1, hyphens and spaces removed. Exactly 10 symbols.
std::optional<std::string> normalize_join_code(std::string_view code);
// 32-byte base64url key (43 characters).
bool valid_overlay_key(std::string_view key);
std::string sha256_base64url(std::string_view value);
}
