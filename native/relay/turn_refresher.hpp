#pragma once
#include "cloudflare_turn.hpp"
#include "ice_directory.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>

namespace odeum::relay {
// Keeps the IceDirectory's dynamic list filled with Cloudflare TURN credentials: fetches at start,
// refreshes at half the lifetime and retries failures with backoff. Runs on the relay's io_context.
// Never logs the API token, credentials or response bodies.
class TurnRefresher : public std::enable_shared_from_this<TurnRefresher> {
public:
    TurnRefresher(boost::asio::io_context& io, CloudflareTurn turn, std::shared_ptr<IceDirectory> directory);
    void start();
private:
    boost::asio::io_context& io_;
    CloudflareTurn turn_;
    std::shared_ptr<IceDirectory> directory_;
    boost::asio::ssl::context tls_;
    boost::asio::steady_timer timer_;
    unsigned failures_ = 0;
    void fetch();
    void finished(bool ok, const std::string& body);
    void schedule(std::chrono::seconds delay);
};

// Loads the platform's trusted root certificates into a TLS client context (Windows: the system
// ROOT store; elsewhere: OpenSSL's default paths).
void load_system_roots(boost::asio::ssl::context& context);
}
