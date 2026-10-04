#pragma once
#include <odeum/ticket.hpp>
#include <map>
#include <mutex>

namespace odeum::relay {
class Authentication {
public:
    explicit Authentication(const Json& keys) : verifier_(keys) {}
    Ticket consume(std::string_view wire, std::int64_t now);
private:
    TicketVerifier verifier_;
    std::mutex mutex_;
    std::map<std::string, std::int64_t> used_;
};
Ticket authorize_sessions(Authentication& auth, std::string_view bearer, std::int64_t now);
}
