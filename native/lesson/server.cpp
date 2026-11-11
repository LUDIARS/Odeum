#include "server.hpp"
#include "connection.hpp"
#include "console_guard.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace odeum::lesson {
namespace {
constexpr auto tick_interval = std::chrono::milliseconds(100);
// Console operations are recorded with ids only: no names, no text typed by anyone.
// Only identifier-shaped values reach the log, so a crafted command cannot forge log lines.
std::string identifier(const std::string& value) {
    const bool plain = !value.empty() && value.size() <= 32 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'; });
    return plain ? value : "-";
}
void audit(const std::string& type, const std::string& class_id, const Json& message, std::string_view result) {
    const auto student = identifier(message.is_object() && message.contains("student") && message["student"].is_string()
        ? message["student"].get<std::string>() : std::string());
    std::clog << "odeum-lesson: console " << identifier(type) << " class=" << (class_id.empty() ? "-" : class_id) << " student=" << student
        << " result=" << result << '\n';
}
}
Millis monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
Server::Server(boost::asio::io_context& io, const Config& config, School& school, std::string console_token)
    : io_(io), config_(config), school_(school), console_token_(std::move(console_token)),
      students_(io, {boost::asio::ip::make_address(config.bind), config.port}),
      console_(io, {boost::asio::ip::make_address("127.0.0.1"), config.console_port}), timer_(io),
      connections_(std::make_shared<std::atomic<std::size_t>>(0)) {}
void Server::start() { accept(students_, false); accept(console_, true); tick(); }
bool Server::console_token_matches(std::string_view presented) const { return same_token(presented, console_token_); }

void Server::accept(boost::asio::ip::tcp::acceptor& acceptor, bool console) {
    acceptor.async_accept([this, &acceptor, console](boost::system::error_code error, boost::asio::ip::tcp::socket socket) {
        if (!error && connections_->load() < max_connections) {
            ++*connections_;
            std::make_shared<Connection>(std::move(socket), *this, console, connections_)->start();
        }
        if (acceptor.is_open()) accept(acceptor, console);
    });
}
void Server::tick() {
    timer_.expires_after(tick_interval);
    timer_.async_wait([this](boost::system::error_code error) {
        if (error) return;
        for (const auto& [class_id, messages] : school_.tick(monotonic_ms())) deliver(class_id, messages);
        tick();
    });
}
void Server::enter(const std::string& class_id, const std::string& student, Connection* connection) {
    seats_[class_id][student].insert(connection);
}
void Server::leave(const std::string& class_id, const std::string& student, Connection* connection) {
    const auto room = seats_.find(class_id);
    if (room == seats_.end()) return;
    if (const auto seat = room->second.find(student); seat != room->second.end()) {
        seat->second.erase(connection);
        if (seat->second.empty()) room->second.erase(seat);
    }
    try { deliver(class_id, school_.classroom(class_id).disconnect(student, monotonic_ms())); } catch (const ProtocolError&) { /* class already closed */ }
}
void Server::console_enter(Connection* connection) {
    if (consoles_.size() >= max_consoles) throw ProtocolError("capacity", "Too many consoles");
    consoles_.emplace(connection, std::string());
    connection->send(school_.catalog());
}
void Server::console_leave(Connection* connection) { consoles_.erase(connection); }

void Server::deliver(const std::string& class_id, const Outbox& messages) {
    const auto room = seats_.find(class_id);
    for (const auto& message : messages) {
        if (message.audience == Audience::consoles) {
            // Who has a tool linked is known here, not in the classroom: add it to what the operator sees.
            auto body = message.body;
            if (body.value("type", Json()) == "snapshot") {
                for (auto& student : body["students"]) student["tool"] = school_.tools().linked(class_id, student.at("id").get<std::string>());
                body["presenter_tool"] = school_.tools().linked(class_id, {});
            }
            for (const auto& [console, attached] : consoles_) if (attached == class_id) console->send(body);
            continue;
        }
        if (room == seats_.end()) continue;
        if (message.audience == Audience::everyone) {
            for (const auto& [student, sockets] : room->second) for (const auto socket : sockets) socket->send(message.body);
            continue;
        }
        const auto seat = room->second.find(message.student);
        if (seat == room->second.end()) continue;
        // Copy: finish() ends in leave(), which edits the set being walked.
        for (const auto socket : std::set<Connection*>(seat->second)) {
            socket->send(message.body);
            if (message.close) socket->finish();
        }
        // A student removed from the class takes their tool with them.
        if (message.close) school_.tools().revoke_student(class_id, message.student);
    }
}
void Server::from_student(const std::string& class_id, const std::string& student, const Json& message) {
    // Link codes are the transport's business: the classroom neither issues nor checks credentials.
    if (message.value("type", Json()) == "tool.link") {
        if (!school_.classroom(class_id).has_student(student)) throw ProtocolError("unknown_student", "Unknown student");
        deliver(class_id, {to_student(student, tool_link({class_id, student, ToolDesk::Role::student}))});
        return;
    }
    deliver(class_id, school_.classroom(class_id).from_student(student, message, monotonic_ms()));
}
void Server::close_class(const std::string& class_id) {
    school_.close(class_id);
    if (const auto room = seats_.find(class_id); room != seats_.end()) {
        const auto seats = room->second;
        for (const auto& [student, sockets] : seats) for (const auto socket : sockets) {
            socket->send({{"type", "class.closed"}});
            socket->finish();
        }
        seats_.erase(class_id);
    }
    for (auto& [console, attached] : consoles_) if (attached == class_id) attached.clear();
}
std::string Server::from_console(Connection* connection, const std::string& current, const Json& message) {
    const auto type = message.is_object() && message.contains("type") && message["type"].is_string() ? message["type"].get<std::string>() : std::string();
    auto attached = current;
    try {
        const auto now = monotonic_ms();
        const auto attach = [&](const std::string& class_id) {
            attached = class_id; consoles_[connection] = class_id;
            auto welcome = school_.classroom(class_id).console_welcome(now);
            welcome["class"] = class_id;
            connection->send(welcome);
        };
        if (type == "catalog") connection->send(school_.catalog());
        else if (type == "class.open") {
            const auto opened = school_.open(message.value("plot", std::string()));
            for (const auto& [console, unused] : consoles_) console->send(school_.catalog());
            attach(opened.id);
        }
        else if (type == "class.select") attach(message.value("class", std::string()));
        else if (type == "class.close") {
            if (current.empty()) throw ProtocolError("unknown_class", "No class selected");
            close_class(current); attached.clear();
            for (const auto& [console, unused] : consoles_) console->send(school_.catalog());
        }
        else if (type == "tool.link") {
            if (current.empty()) throw ProtocolError("unknown_class", "No class selected");
            school_.classroom(current);
            // Only to the console that asked. Tools talk to the students' listener, so its port goes along.
            auto link = tool_link({current, {}, ToolDesk::Role::presenter});
            link["port"] = config_.port;
            connection->send(std::move(link));
        }
        else {
            if (current.empty()) throw ProtocolError("unknown_class", "No class selected");
            deliver(current, school_.classroom(current).from_console(message, now));
        }
        audit(type, attached, message, "ok");
    } catch (const ProtocolError& error) {
        audit(type, attached, message, error.code);
        connection->send(error_message(error.code, error.what()));
    }
    return attached;
}
}
