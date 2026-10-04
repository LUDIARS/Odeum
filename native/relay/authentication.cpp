#include "authentication.hpp"

namespace odeum::relay {
Ticket Authentication::consume(std::string_view wire, std::int64_t now) {
    auto ticket = verifier_.verify(wire, now);
    std::lock_guard lock(mutex_);
    std::erase_if(used_, [now](const auto& entry) { return entry.second <= now; });
    if (used_.contains(ticket.jti)) throw ProtocolError("replayed_ticket", "Ticket already used");
    if (used_.size() >= 100000) throw ProtocolError("capacity", "Authentication capacity reached");
    used_.emplace(ticket.jti, ticket.exp); return ticket;
}
Ticket authorize_sessions(Authentication& auth, std::string_view bearer, std::int64_t now) {
    if (!bearer.starts_with("Bearer ")) throw ProtocolError("invalid_ticket", "Bearer ticket required");
    auto ticket = auth.consume(bearer.substr(7), now);
    if (ticket.role != Role::service) throw ProtocolError("forbidden", "Service role required");
    return ticket;
}
}
