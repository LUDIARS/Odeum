#pragma once
// HTTP/1.1 + WebSocket listener (Boost.Beast). One instance serves every configured port with
// the same handler, as the former Node runner listened on 4649 and 7676 with one Hono app.
#include "../core/web_access.hpp"
#include "http_types.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <atomic>
#include <chrono>

namespace odeum::runner {
struct ServerLimits {
    std::size_t max_connections = 512;          // beyond: the connection is closed at accept (refuse)
    std::size_t max_ws_sessions = 128;          // beyond: 503 to the upgrade (refuse)
    std::size_t header_bytes = 16 * 1024;       // beyond: 431
    std::size_t body_bytes = 256 * 1024;        // beyond: 413 Payload Too Large
    std::chrono::seconds request_timeout{30};   // reading one request (headers and body)
    std::chrono::seconds idle_timeout{60};      // keep-alive idle and WebSocket ping timeout
    std::size_t ws_message_bytes = 64 * 1024;   // one browser message; beyond: closed (1009)
    unsigned ws_messages_per_second = 240;      // beyond: closed (1008)
    std::size_t ws_queue_bytes = 64u * 1024 * 1024;           // outgoing queue; beyond: closed
    std::size_t ws_droppable_queue_bytes = 8u * 1024 * 1024;  // screenshots are skipped above this
};

class HttpServer {
public:
    HttpServer(boost::asio::io_context& io, RequestHandler& handler, WsEndpoint& ws, WebAccessPolicy access, ServerLimits limits = {});
    ~HttpServer();
    // Throws boost::system::system_error when the address cannot be bound.
    std::uint16_t listen(const std::string& host, std::uint16_t port, const std::string& label);
    void stop();
    struct Shared;
private:
    std::shared_ptr<Shared> shared_;
    std::vector<std::shared_ptr<boost::asio::ip::tcp::acceptor>> acceptors_;
    void accept(std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor);
};
}
