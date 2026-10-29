#pragma once
// STUN from the environment plus, with a Cloudflare TURN key, short-lived TURN credentials issued over
// HTTPS (10 s deadline, 64 KiB response budget), cached until one hour before expiry. A failed
// issue degrades to STUN only. The API token is sent only to the fixed Cloudflare endpoint and is
// never logged.
#include "../core/ice.hpp"
#include <boost/asio/io_context.hpp>
#include <functional>

namespace odeum::runner {
class IceServerProvider {
public:
    IceServerProvider(boost::asio::io_context& io, const std::optional<std::string>& stun_env, std::string turn_key_id, std::string turn_token,
        std::function<long long()> now_ms);
    bool has_turn() const { return !turn_key_id_.empty(); }
    void get(std::function<void(std::vector<IceServer>)> done);
private:
    boost::asio::io_context& io_;
    std::vector<IceServer> stun_;
    std::string turn_key_id_, turn_token_;
    std::function<long long()> now_;
    std::optional<std::pair<std::vector<IceServer>, long long>> turn_;  // servers, expires_at
    std::vector<std::function<void(std::vector<IceServer>)>> pending_;
};
}
