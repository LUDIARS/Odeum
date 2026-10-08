#include "invitations.hpp"
#include <openssl/evp.h>
#include <array>
#include <cctype>

namespace odeum::relay {
namespace {
constexpr std::string_view crockford = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
[[noreturn]] void not_live() { throw ProtocolError("session_not_live", "No live session for this invitation"); }
}
std::optional<std::string> normalize_join_code(std::string_view code) {
    if (code.size() > 32) return std::nullopt;
    std::string out;
    for (unsigned char c : code) {
        if (c == '-' || c == ' ') continue;
        auto upper = static_cast<char>(std::toupper(c));
        if (upper == 'O') upper = '0'; else if (upper == 'I' || upper == 'L') upper = '1';
        if (crockford.find(upper) == std::string_view::npos) return std::nullopt;
        out.push_back(upper);
    }
    if (out.size() != 10) return std::nullopt;
    return out;
}
bool valid_overlay_key(std::string_view key) {
    if (key.size() != 43) return false;
    for (unsigned char c : key) if (!std::isalnum(c) && c != '-' && c != '_') return false;
    // The final symbol carries 2 padding bits that must be zero for a canonical 32-byte value.
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    return (alphabet.find(key.back()) & 3u) == 0;
}
std::string sha256_base64url(std::string_view value) {
    std::array<unsigned char, 32> hash{}; unsigned int length = 0;
    if (EVP_Digest(value.data(), value.size(), hash.data(), &length, EVP_sha256(), nullptr) != 1 || length != hash.size())
        throw std::runtime_error("SHA-256 unavailable");
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out; unsigned bits = 0, buffer = 0;
    for (auto byte : hash) {
        buffer = (buffer << 8) | byte; bits += 8;
        while (bits >= 6) { bits -= 6; out.push_back(alphabet[(buffer >> bits) & 63u]); }
    }
    if (bits) out.push_back(alphabet[(buffer << (6 - bits)) & 63u]);
    return out;
}
void Invitations::attach(const std::string& sid, const std::string& join_digest, const std::string& overlay_digest) {
    if (join_digest.empty() || overlay_digest.empty()) return;
    auto owned = [&](const std::map<std::string, std::string>& index, const std::string& digest) {
        auto it = index.find(digest); return it == index.end() || it->second == sid;
    };
    if (!owned(joins_, join_digest) || !owned(overlays_, overlay_digest) || !owned(joins_, overlay_digest) || !owned(overlays_, join_digest))
        throw ProtocolError("invite_conflict", "Invitation belongs to another session");
    detach(sid);
    sessions_[sid] = {join_digest, overlay_digest};
    joins_[join_digest] = sid; overlays_[overlay_digest] = sid;
}
void Invitations::detach(const std::string& sid) {
    auto it = sessions_.find(sid); if (it == sessions_.end()) return;
    joins_.erase(it->second.join); overlays_.erase(it->second.overlay); sessions_.erase(it);
}
std::string Invitations::resolve(std::map<std::string, std::string>& index, const std::optional<std::string>& digest, Millis now) {
    while (!failures_.empty() && now - failures_.front() >= 60000) failures_.pop_front();
    if (failures_.size() >= max_failures_per_minute) throw ProtocolError("rate_limited", "Too many invitation attempts");
    if (digest) { auto it = index.find(*digest); if (it != index.end()) return it->second; }
    failures_.push_back(now); not_live();
}
std::string Invitations::resolve_join(std::string_view code, Millis now) {
    auto normalized = normalize_join_code(code);
    return resolve(joins_, normalized ? std::optional(sha256_base64url(*normalized)) : std::nullopt, now);
}
std::string Invitations::resolve_overlay(std::string_view key, Millis now) {
    return resolve(overlays_, valid_overlay_key(key) ? std::optional(sha256_base64url(key)) : std::nullopt, now);
}
}
