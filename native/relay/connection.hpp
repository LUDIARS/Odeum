#pragma once
#include "server.hpp"
#include "web_assets.hpp"
#include <boost/beast.hpp>
#include <deque>

namespace odeum::relay {
class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(boost::asio::ip::tcp::socket socket, Authentication& auth, Hub& hub,
        std::uint64_t id, std::shared_ptr<std::atomic<std::size_t>> connections, const WebAccess& access);
    ~Connection();
    void start();
private:
    boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
    boost::beast::flat_buffer buffer_;
    boost::beast::http::request_parser<boost::beast::http::string_body> parser_;
    Authentication& auth_;
    Hub& hub_;
    const WebAccess& access_;
    std::uint64_t id_;
    std::shared_ptr<std::atomic<std::size_t>> connections_;
    std::optional<Ticket> ticket_;
    bool joined_ = false, closed_ = false, close_after_write_ = false;
    std::deque<std::string> writes_;
    std::size_t queued_bytes_ = 0;
    Millis window_ = 0;
    unsigned messages_ = 0;
    using Request = boost::beast::http::request<boost::beast::http::string_body>;
    void route();
    void asset(const WebAsset& page);
    void upgrade(const Request& request, Admission admission, std::function<Ticket()> admit);
    void response(unsigned status, const Json& body);
    void read();
    void send(const Json& body);
    void write();
    void close();
};
}
