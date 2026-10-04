#pragma once
#include "authentication.hpp"
#include "hub.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

namespace odeum::relay {
class Server {
public:
    Server(boost::asio::io_context& io, const Config& config);
    void start();
private:
    boost::asio::io_context& io_;
    const Config& config_;
    Authentication auth_;
    Hub hub_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::steady_timer timer_;
    std::uint64_t next_id_ = 0;
    std::shared_ptr<std::atomic<std::size_t>> connections_;
    void accept();
    void tick();
};
Millis monotonic_ms();
std::int64_t epoch_ms();
}
