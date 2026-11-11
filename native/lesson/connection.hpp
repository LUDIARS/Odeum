#pragma once
#include "server.hpp"
#include <boost/beast.hpp>
#include <deque>
#include <optional>

namespace odeum::lesson {
// One HTTP request or one WebSocket session, for a student or for the operator console.
class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(boost::asio::ip::tcp::socket socket, Server& server, bool console, std::shared_ptr<std::atomic<std::size_t>> connections);
    ~Connection();
    void start();
    void send(const Json& body);
    // Delivers what is already queued, then closes (used when a student is removed from the class).
    void finish();
private:
    using Request = boost::beast::http::request<boost::beast::http::string_body>;
    boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
    boost::beast::flat_buffer buffer_;
    boost::beast::http::request_parser<boost::beast::http::string_body> parser_;
    Server& server_;
    const bool console_;
    std::shared_ptr<std::atomic<std::size_t>> connections_;
    bool peer_loopback_ = false, joined_ = false, closed_ = false, close_after_write_ = false;
    std::string class_, student_;
    std::deque<std::string> writes_;
    std::size_t queued_bytes_ = 0;
    Millis window_ = 0;
    unsigned messages_ = 0;

    void route();
    void route_students(const Request& request);
    void route_console(const Request& request);
    bool serve_shared(const Request& request, const std::string& path, const std::map<std::string, std::string>& query);
    void respond(unsigned status, const Json& body);
    void respond_bytes(std::string body, std::string_view content_type, std::string_view disposition, bool page);
    void upgrade(const Request& request, std::function<void()> admit);
    void read();
    void write();
    void close();
};
}
