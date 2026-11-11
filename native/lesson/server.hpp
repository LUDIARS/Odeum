#pragma once
#include "config.hpp"
#include "school.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <atomic>
#include <set>

namespace odeum::lesson {
class Connection;
Millis monotonic_ms();
// Owns the two listeners (students / operator console) and routes classroom output to sockets.
// Everything runs on one io_context thread, like the relay.
class Server {
public:
    static constexpr std::size_t max_connections = 2048, max_consoles = 8;
    Server(boost::asio::io_context& io, const Config& config, School& school, std::string console_token);
    void start();

    const Config& config() const { return config_; }
    School& school() { return school_; }
    bool console_token_matches(std::string_view presented) const;

    // Called by connections once their WebSocket is open.
    void enter(const std::string& class_id, const std::string& student, Connection* connection);
    void leave(const std::string& class_id, const std::string& student, Connection* connection);
    void console_enter(Connection* connection);
    void console_leave(Connection* connection);
    void from_student(const std::string& class_id, const std::string& student, const Json& message);
    // Returns the class the console is now attached to (commands can open or switch classes).
    std::string from_console(Connection* connection, const std::string& class_id, const Json& message);
    void deliver(const std::string& class_id, const Outbox& messages);
    // server_tools.cpp: tools on participants' machines (HTTP, one request at a time).
    Json tool_pair(const Json& body);
    Json tool_state(std::string_view token);
    Json tool_progress(std::string_view token, const Json& body);
private:
    boost::asio::io_context& io_;
    const Config& config_;
    School& school_;
    std::string console_token_;
    boost::asio::ip::tcp::acceptor students_, console_;
    boost::asio::steady_timer timer_;
    std::shared_ptr<std::atomic<std::size_t>> connections_;
    std::map<std::string, std::map<std::string, std::set<Connection*>>> seats_;  // class -> student -> sockets
    std::map<Connection*, std::string> consoles_;                               // console -> attached class
    void accept(boost::asio::ip::tcp::acceptor& acceptor, bool console);
    void tick();
    void close_class(const std::string& class_id);
    Json tool_link(const ToolDesk::Holder& holder);
};
}
