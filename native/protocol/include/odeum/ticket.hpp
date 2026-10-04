#pragma once
#include "message.hpp"
#include <memory>
#include <unordered_map>

namespace odeum {
struct Ticket { std::string sub, name, sid, jti; Role role; std::int64_t exp; };
class TicketVerifier {
public:
    explicit TicketVerifier(const Json& public_keys);
    Ticket verify(std::string_view compact, std::int64_t now) const;
private:
    struct Key;
    std::unordered_map<std::string, std::shared_ptr<Key>> keys_;
};
}
