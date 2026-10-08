#pragma once
#include "message.hpp"
#include <memory>
#include <unordered_map>

namespace odeum {
// invite_join / invite_overlay: base64url SHA-256 of the GLab-held guest code and overlay key.
// Only presenter tickets may carry them; empty means the room offers no guest/overlay entry.
struct Ticket { std::string sub, name, sid, jti; Role role; std::int64_t exp; std::string invite_join = {}, invite_overlay = {}; };
class TicketVerifier {
public:
    explicit TicketVerifier(const Json& public_keys);
    Ticket verify(std::string_view compact, std::int64_t now) const;
private:
    struct Key;
    std::unordered_map<std::string, std::shared_ptr<Key>> keys_;
};
}
