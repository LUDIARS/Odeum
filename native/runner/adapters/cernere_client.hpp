#pragma once
// Token verification against Cernere POST /api/auth/verify with a 5-minute cache
// (custos/src/auth/cernere-auth.ts). Added bounds: a 10 s deadline, a 64 KiB response budget, at
// most 2048 cached tokens and 32 verifications in flight (beyond: rejected as unauthorized).
#include "../core/auth_policy.hpp"
#include <boost/asio/io_context.hpp>
#include <functional>
#include <map>
#include <vector>

namespace odeum::runner {
class CernereAuth {
public:
    CernereAuth(boost::asio::io_context& io, AuthSettings settings, std::function<long long()> now_ms);
    const AuthSettings& settings() const { return settings_; }
    void verify(const std::string& token, std::function<void(std::optional<VerifiedUser>)> done);
    void invalidate(const std::string& token) { cache_.erase(token); }
private:
    struct Entry { VerifiedUser user; long long expires_at; };
    boost::asio::io_context& io_;
    AuthSettings settings_;
    std::function<long long()> now_;
    std::map<std::string, Entry> cache_;
    std::map<std::string, std::vector<std::function<void(std::optional<VerifiedUser>)>>> pending_;
};
}
