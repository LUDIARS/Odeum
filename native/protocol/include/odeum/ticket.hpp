#pragma once
#include "message.hpp"
#include "slot.hpp"
#include <memory>
#include <unordered_map>

namespace odeum {
// invite_join / invite_overlay: base64url SHA-256 of the GLab-held guest code and overlay key.
// Presenter and producer tickets may carry them; empty means the room offers no guest/overlay entry.
// slot: the media slot a presenter publishes to (input1..input8 or program). Only presenter and producer
// tickets carry it; the verifier fills program when the claim is omitted. A producer always holds program.
struct Ticket { std::string sub, name, sid, jti; Role role; std::int64_t exp; std::string invite_join = {}, invite_overlay = {};
    std::string slot = {}; };
// The slot a sending participant occupies (program when unset), or empty for receive-only roles.
std::string sender_slot(const Ticket& ticket);
class TicketVerifier {
public:
    explicit TicketVerifier(const Json& public_keys);
    Ticket verify(std::string_view compact, std::int64_t now) const;
private:
    struct Key;
    std::unordered_map<std::string, std::shared_ptr<Key>> keys_;
};
}
