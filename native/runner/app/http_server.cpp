#include "http_server.hpp"
#include "../adapters/clock.hpp"
#include "../adapters/log.hpp"
#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <algorithm>
#include <deque>

namespace odeum::runner {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

struct HttpServer::Shared {
    Shared(asio::io_context& context, RequestHandler& h, WsEndpoint& w, WebAccessPolicy a, ServerLimits l)
        : io(context), handler(h), ws(w), access(std::move(a)), limits(l) {}
    asio::io_context& io;
    RequestHandler& handler;
    WsEndpoint& ws;
    WebAccessPolicy access;
    ServerLimits limits;
    std::atomic<std::size_t> connections{0};
    std::atomic<std::size_t> ws_sessions{0};
    bool stopped = false;
};

namespace {
using Shared = HttpServer::Shared;

class WsSession : public WsPeer, public std::enable_shared_from_this<WsSession> {
public:
    WsSession(beast::tcp_stream stream, std::shared_ptr<Shared> shared)
        : ws_(std::move(stream)), shared_(std::move(shared)) { ++shared_->ws_sessions; }
    ~WsSession() override { --shared_->ws_sessions; }

    void start(http::request<http::string_body> upgrade, ServerRequest request) {
        auto self = shared_from_this();
        beast::get_lowest_layer(ws_).expires_never();
        auto timeout = websocket::stream_base::timeout::suggested(beast::role_type::server);
        timeout.idle_timeout = shared_->limits.idle_timeout;
        timeout.keep_alive_pings = true;
        ws_.set_option(timeout);
        ws_.read_message_max(shared_->limits.ws_message_bytes);
        upgrade_ = std::move(upgrade);
        ws_.async_accept(upgrade_, [self, request = std::move(request)](const beast::error_code& error) {
            if (error) return self->fail();
            // As the former broker: the handshake completes, then the token is checked.
            self->shared_->ws.authorize(request, [self](std::optional<VerifiedUser> user) {
                if (self->closed_) return;
                if (!user) { self->close(4401, "unauthorized"); return; }
                self->id_ = self->shared_->ws.open(self, *user);
                self->opened_ = true;
                self->read();
            });
        });
    }

    void send_text(std::string text, bool droppable) override {
        if (closed_ || closing_) return;
        if (droppable && queued_ + text.size() > shared_->limits.ws_droppable_queue_bytes) return;  // skip, the next frame replaces it
        if (queued_ + text.size() > shared_->limits.ws_queue_bytes) {
            log::warn("ws", "outgoing queue full — closing", {{"sessionId", id_}});
            close(1008, "slow consumer");
            return;
        }
        queued_ += text.size();
        writes_.push_back(std::move(text));
        if (writes_.size() == 1) write();
    }

    void close(unsigned short code, const std::string& reason) override {
        if (closed_ || closing_) return;
        closing_ = true;
        // Close after the queued frames, as ws.close() does after pending sends.
        close_code_ = code;
        close_reason_ = reason;
        if (writes_.empty()) do_close();
    }

private:
    websocket::stream<beast::tcp_stream> ws_;
    std::shared_ptr<Shared> shared_;
    http::request<http::string_body> upgrade_;
    beast::flat_buffer buffer_;
    std::deque<std::string> writes_;
    std::size_t queued_ = 0;
    std::string id_;
    bool opened_ = false, closed_ = false, closing_ = false;
    unsigned short close_code_ = 1000;
    std::string close_reason_;
    long long window_ = 0;
    unsigned messages_ = 0;

    void do_close() {
        auto self = shared_from_this();
        ws_.async_close(websocket::close_reason(static_cast<websocket::close_code>(close_code_), close_reason_), [self](const beast::error_code&) { self->fail(); });
    }

    void read() {
        if (closed_) return;
        auto self = shared_from_this();
        ws_.async_read(buffer_, [self](const beast::error_code& error, std::size_t) {
            if (error) {
                if (error == websocket::error::message_too_big) log::warn("ws", "message too large — closing", {{"sessionId", self->id_}});
                return self->fail();
            }
            const auto now = monotonic_ms();
            if (now - self->window_ >= 1000) { self->window_ = now; self->messages_ = 0; }
            if (++self->messages_ > self->shared_->limits.ws_messages_per_second) {
                log::warn("ws", "message rate exceeded — closing", {{"sessionId", self->id_}});
                self->buffer_.consume(self->buffer_.size());
                self->close(1008, "rate limit");
                return;
            }
            // Binary frames are not part of the protocol and are ignored, as before.
            if (self->ws_.got_text()) {
                const auto text = beast::buffers_to_string(self->buffer_.data());
                self->shared_->ws.message(self->id_, text);
            }
            self->buffer_.consume(self->buffer_.size());
            self->read();
        });
    }

    void write() {
        if (closed_ || writes_.empty()) return;
        auto self = shared_from_this();
        ws_.text(true);
        ws_.async_write(asio::buffer(writes_.front()), [self](const beast::error_code& error, std::size_t) {
            if (error) return self->fail();
            self->queued_ -= self->writes_.front().size();
            self->writes_.pop_front();
            if (!self->writes_.empty()) return self->write();
            if (self->closing_) self->do_close();
        });
    }

    void fail() {
        if (closed_) return;
        closed_ = true;
        beast::error_code ignored;
        beast::get_lowest_layer(ws_).socket().shutdown(tcp::socket::shutdown_both, ignored);
        beast::get_lowest_layer(ws_).close();
        writes_.clear();
        queued_ = 0;
        if (opened_) { opened_ = false; shared_->ws.closed(id_); }
    }
};

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(tcp::socket socket, std::shared_ptr<Shared> shared) : stream_(std::move(socket)), shared_(std::move(shared)) { ++shared_->connections; }
    ~Connection() { --shared_->connections; }
    void start() { read(); }

private:
    beast::tcp_stream stream_;
    std::shared_ptr<Shared> shared_;
    beast::flat_buffer buffer_;
    std::optional<http::request_parser<http::string_body>> parser_;
    bool upgraded_ = false;

    void read() {
        parser_.emplace();
        parser_->header_limit(static_cast<std::uint32_t>(shared_->limits.header_bytes));
        parser_->body_limit(shared_->limits.body_bytes);
        stream_.expires_after(shared_->limits.request_timeout);
        auto self = shared_from_this();
        http::async_read(stream_, buffer_, *parser_, [self](const beast::error_code& error, std::size_t) {
            if (error == http::error::body_limit) return self->respond_and_close(413, "Payload Too Large");
            if (error == http::error::header_limit) return self->respond_and_close(431, "Request Header Fields Too Large");
            if (error == http::error::end_of_stream || error == beast::error::timeout || error == asio::error::operation_aborted) return self->close();
            if (error) return self->respond_and_close(400, "Bad Request");
            self->route();
        });
    }

    void respond_and_close(unsigned status, const char* text) {
        auto response = ServerResponse::text(status, text);
        write(std::move(response), false, false);
    }

    void route() {
        auto message = parser_->release();
        ServerRequest request;
        request.method = std::string(message.method_string());
        request.target = std::string(message.target());
        const auto question = request.target.find('?');
        request.path = request.target.substr(0, question);
        if (question != std::string::npos) request.query = parse_query(std::string_view(request.target).substr(question + 1));
        for (const auto& field : message) {
            std::string name(field.name_string());
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            request.headers.emplace(std::move(name), std::string(field.value()));
        }
        request.body = message.body();
        const auto host = request.header("host").value_or("");
        const auto origin = request.header("origin").value_or("");
        const bool keep_alive = message.keep_alive();
        const bool head = request.method == "HEAD";

        if (!host_allowed(shared_->access, host)) return write(ServerResponse::json(403, Json{{"error", "forbidden_host"}}), false, head);
        if (websocket::is_upgrade(message)) {
            if (request.path != "/ws") return write(ServerResponse::text(400, "Bad Request"), false, head);
            if (!origin_allowed(shared_->access, origin, host)) return write(ServerResponse::json(403, Json{{"error", "cross_origin_forbidden"}}), false, head);
            if (shared_->ws_sessions.load() >= shared_->limits.max_ws_sessions) return write(ServerResponse::json(503, Json{{"error", "too_many_sessions"}}), false, head);
            upgraded_ = true;
            auto session = std::make_shared<WsSession>(std::move(stream_), shared_);
            session->start(std::move(message), std::move(request));
            return;
        }
        if (is_state_changing(request.method) && !origin_allowed(shared_->access, origin, host)) {
            return write(ServerResponse::json(403, Json{{"error", "cross_origin_forbidden"}}), false, head);
        }
        const auto started = monotonic_ms();
        // The pending reply keeps the connection alive until the handler answers.
        auto self = shared_from_this();
        auto replied = std::make_shared<bool>(false);
        auto io = &shared_->io;
        const auto method = request.method, path = request.path;
        shared_->handler.handle(request, [self, replied, io, keep_alive, head, started, method, path](ServerResponse response) {
            if (*replied) return;
            *replied = true;
            log::info("http", "request", {{"method", method}, {"path", path}, {"status", response.status}, {"ms", monotonic_ms() - started}});
            asio::post(*io, [self, response = std::move(response), keep_alive, head]() mutable {
                self->write(std::move(response), keep_alive, head);
            });
        });
    }

    void write(ServerResponse response, bool keep_alive, bool head) {
        auto result = std::make_shared<http::response<http::string_body>>(static_cast<http::status>(response.status), 11);
        if (!response.content_type.empty()) result->set(http::field::content_type, response.content_type);
        // The former app-wide middleware: nothing is cached.
        result->set(http::field::cache_control, "no-store, no-cache, must-revalidate");
        result->set(http::field::pragma, "no-cache");
        result->set("X-Content-Type-Options", "nosniff");
        for (const auto& [name, value] : response.headers) result->set(name, value);
        result->keep_alive(keep_alive && !shared_->stopped);
        const auto size = response.body.size();
        if (!head) result->body() = std::move(response.body);
        result->prepare_payload();
        if (head) result->content_length(size);
        stream_.expires_after(std::chrono::seconds(60));
        auto self = shared_from_this();
        http::async_write(stream_, *result, [self, result](const beast::error_code& error, std::size_t) {
            if (error || !result->keep_alive()) return self->close();
            self->read();
        });
    }

    void close() {
        if (upgraded_) return;
        beast::error_code ignored;
        stream_.socket().shutdown(tcp::socket::shutdown_both, ignored);
        stream_.close();
    }
};
}

HttpServer::HttpServer(asio::io_context& io, RequestHandler& handler, WsEndpoint& ws, WebAccessPolicy access, ServerLimits limits)
    : shared_(std::make_shared<Shared>(io, handler, ws, std::move(access), limits)) {}

HttpServer::~HttpServer() { stop(); }

std::uint16_t HttpServer::listen(const std::string& host, std::uint16_t port, const std::string& label) {
    auto acceptor = std::make_shared<tcp::acceptor>(shared_->io);
    const tcp::endpoint endpoint(asio::ip::make_address(host == "localhost" ? "127.0.0.1" : host), port);
    acceptor->open(endpoint.protocol());
    acceptor->set_option(tcp::acceptor::reuse_address(true));
    acceptor->bind(endpoint);
    acceptor->listen(asio::socket_base::max_listen_connections);
    const auto bound = acceptor->local_endpoint().port();
    log::info("http", "listening", {{"host", host}, {"port", bound}, {"label", label}});
    acceptors_.push_back(acceptor);
    accept(acceptor);
    return bound;
}

void HttpServer::accept(std::shared_ptr<tcp::acceptor> acceptor) {
    auto shared = shared_;
    acceptor->async_accept([this, acceptor, shared](const beast::error_code& error, tcp::socket socket) {
        if (error) {
            if (error == asio::error::operation_aborted || shared->stopped) return;
        } else if (shared->connections.load() < shared->limits.max_connections) {
            std::make_shared<Connection>(std::move(socket), shared)->start();
        } else {
            beast::error_code ignored;
            socket.close(ignored);  // refuse: connection limit reached
        }
        if (acceptor->is_open() && !shared->stopped) accept(acceptor);
    });
}

void HttpServer::stop() {
    shared_->stopped = true;
    for (auto& acceptor : acceptors_) {
        beast::error_code ignored;
        acceptor->close(ignored);
    }
    acceptors_.clear();
}
}
