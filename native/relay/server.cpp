#include "server.hpp"
#include "connection.hpp"

namespace odeum::relay {
Millis monotonic_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::int64_t epoch_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
Server::Server(boost::asio::io_context& io, const Config& config) : io_(io), config_(config), auth_(config.keys), hub_(io, config),
    acceptor_(io, {boost::asio::ip::make_address(config.bind), config.port}), timer_(io), connections_(std::make_shared<std::atomic<std::size_t>>(0)) {}
void Server::start() { accept(); tick(); }
void Server::accept() {
    acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!ec && connections_->load() < config_.max_sessions * (config_.max_viewers + 1) + 64) {
            ++*connections_; std::make_shared<Connection>(std::move(socket), auth_, hub_, ++next_id_, connections_, config_.web_access)->start();
        }
        if (acceptor_.is_open()) accept();
    });
}
void Server::tick() {
    timer_.expires_after(std::chrono::milliseconds(25));
    timer_.async_wait([this](boost::system::error_code ec) { if (!ec) { hub_.tick(monotonic_ms()); tick(); } });
}
}
