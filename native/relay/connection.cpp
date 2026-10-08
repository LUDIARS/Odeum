#include "connection.hpp"
#include "admission.hpp"
#include "http_query.hpp"
#include "web_assets.hpp"
#include <boost/asio/post.hpp>

namespace odeum::relay {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
Connection::Connection(boost::asio::ip::tcp::socket socket, Authentication& auth, Hub& hub,
    std::uint64_t id, std::shared_ptr<std::atomic<std::size_t>> connections, const WebAccess& access)
    : ws_(std::move(socket)), auth_(auth), hub_(hub), access_(access), id_(id), connections_(std::move(connections)) {}
Connection::~Connection() { --*connections_; }
void Connection::start() {
    parser_.body_limit(0); parser_.header_limit(16384);
    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));
    http::async_read(ws_.next_layer(), buffer_, parser_, [self = shared_from_this()](beast::error_code ec, std::size_t) {
        if (ec) { self->close(); return; } self->route();
    });
}
void Connection::response(unsigned status, const Json& body) {
    auto result = std::make_shared<http::response<http::string_body>>(static_cast<http::status>(status), 11);
    result->set(http::field::content_type, "application/json; charset=utf-8");
    result->set(http::field::cache_control, "no-store"); result->keep_alive(false);
    result->body() = body.dump(); result->prepare_payload();
    http::async_write(ws_.next_layer(), *result, [self = shared_from_this(), result](beast::error_code, std::size_t) { self->close(); });
}
namespace {
unsigned http_status(const std::string& code) {
    if (code == "forbidden") return 403;
    if (code == "session_not_live") return 404;
    if (code == "rate_limited") return 429;
    if (code == "invalid_name" || code == "invalid_request") return 400;
    return 401;
}
bool self_served(const std::optional<HttpTarget>& target) {
    return target && (find_web_asset(target->path) || target->path == "/v1/guest" || target->path == "/v1/overlay");
}
}
void Connection::asset(const WebAsset& page) {
    auto result = std::make_shared<http::response<http::string_body>>(http::status::ok, 11);
    result->set(http::field::content_type, page.content_type);
    result->set(http::field::cache_control, "no-cache");
    result->set("X-Content-Type-Options", "nosniff");
    result->set("Referrer-Policy", "no-referrer");
    result->set("Content-Security-Policy",
        "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self' ws: wss:; img-src 'self' data:; base-uri 'none'; form-action 'none'");
    result->keep_alive(false); result->body() = std::string(page.body); result->prepare_payload();
    http::async_write(ws_.next_layer(), *result, [self = shared_from_this(), result](beast::error_code, std::size_t) { self->close(); });
}
void Connection::route() {
    try {
        const auto& request = parser_.get(); const std::string target(request.target());
        const std::string authority(request[http::field::host]), origin(request[http::field::origin]);
        const auto parsed = parse_target(target);
        const bool allowed = self_served(parsed) ? access_.allows_same_origin(authority, origin)
            : access_.allows_host(authority) && access_.allows_origin(origin);
        if (!allowed) { response(403, {{"error", "web_access_denied"}}); return; }
        if (request.method() != http::verb::get) { response(405, {{"error", "method_not_allowed"}}); return; }
        if (target == "/health") { response(200, {{"ok", true}, {"version", "0.1.0"}}); return; }
        if (target == "/v1/sessions") {
            authorize_sessions(auth_, std::string(request[http::field::authorization]), epoch_ms() / 1000);
            response(200, hub_.sessions()); return;
        }
        if (parsed) {
            if (auto page = find_web_asset(parsed->path)) { asset(*page); return; }
            if (parsed->path == "/v1/guest" || parsed->path == "/v1/overlay") {
                const bool guest = parsed->path == "/v1/guest";
                const auto credential = parsed->query.find(guest ? "code" : "key");
                const auto name = parsed->query.find("name");
                if (credential == parsed->query.end() || (guest && name == parsed->query.end()) || parsed->query.size() != (guest ? 2u : 1u))
                    throw ProtocolError("invalid_request", "Invitation parameters required");
                // Resolve after the upgrade so the page can show why it was refused.
                upgrade(request, guest ? Admission::guest : Admission::overlay,
                    [this, guest, code = credential->second, display = guest ? name->second : std::string()] {
                        auto& invitations = hub_.invitations();
                        if (!guest) return overlay_ticket(invitations.resolve_overlay(code, monotonic_ms()));
                        auto sid = invitations.resolve_join(code, monotonic_ms());
                        return guest_ticket(sid, display);
                    });
                return;
            }
        }
        constexpr std::string_view prefix = "/v1/ws?ticket=";
        if (!target.starts_with(prefix)) { response(404, {{"error", "not_found"}}); return; }
        if (!websocket::is_upgrade(request)) { response(400, {{"error", "upgrade_required"}}); return; }
        ticket_ = auth_.consume(std::string_view(target).substr(prefix.size()), epoch_ms() / 1000);
        if (ticket_->role == Role::service) { response(403, {{"error", "forbidden"}}); return; }
        upgrade(request, Admission::ticket, [ticket = *ticket_] { return ticket; });
    } catch (const ProtocolError& error) { response(http_status(error.code), {{"error", error.code}}); }
    catch (const std::exception&) { response(400, {{"error", "invalid_request"}}); }
}
void Connection::upgrade(const Request& request, Admission admission, std::function<Ticket()> admit) {
    if (!websocket::is_upgrade(request)) { response(400, {{"error", "upgrade_required"}}); return; }
    beast::get_lowest_layer(ws_).expires_never();
    auto timeout = websocket::stream_base::timeout::suggested(beast::role_type::server);
    timeout.idle_timeout = std::chrono::seconds(60); timeout.keep_alive_pings = true;
    ws_.set_option(timeout); ws_.read_message_max(max_message_bytes);
    ws_.async_accept(request, [self = shared_from_this(), admission, admit = std::move(admit)](beast::error_code ec) {
        if (ec) { self->close(); return; }
        try {
            self->ticket_ = admit();
            auto weak = self->weak_from_this();
            self->hub_.join(self->id_, *self->ticket_, [weak](const Json& body) { if (auto s = weak.lock()) s->send(body); },
                [weak] { if (auto s = weak.lock()) boost::asio::post(s->ws_.get_executor(), [weak] { if (auto c = weak.lock()) c->close(); }); },
                monotonic_ms(), epoch_ms(), admission);
            self->joined_ = true; self->read();
        } catch (const ProtocolError& error) { self->close_after_write_ = true; self->send(error_message(error.code, error.what())); }
        catch (const std::exception&) { self->close(); }
    });
}
void Connection::read() {
    if (closed_) return;
    ws_.async_read(buffer_, [self = shared_from_this()](beast::error_code ec, std::size_t) {
        if (ec) { self->close(); return; }
        try {
            if (!self->joined_) { self->close(); return; }
            if (!self->ws_.got_text()) throw ProtocolError("invalid_message", "Text frames required");
            auto now = monotonic_ms();
            if (now - self->window_ >= 1000) { self->window_ = now; self->messages_ = 0; }
            if (++self->messages_ > 120) { self->close(); return; }
            auto message = parse_message(beast::buffers_to_string(self->buffer_.data()));
            self->hub_.message(self->id_, *self->ticket_, message, now, epoch_ms());
        } catch (const ProtocolError& error) { self->send(error_message(error.code, error.what())); }
        catch (const std::exception&) { self->send(error_message("invalid_message", "Message rejected")); }
        self->buffer_.consume(self->buffer_.size()); self->read();
    });
}
void Connection::send(const Json& body) {
    if (closed_) return;
    auto wire = body.dump();
    if (wire.size() > max_message_bytes || queued_bytes_ + wire.size() > 256 * 1024 || writes_.size() >= 256) {
        boost::asio::post(ws_.get_executor(), [self = shared_from_this()] { self->close(); }); return;
    }
    queued_bytes_ += wire.size(); writes_.push_back(std::move(wire)); if (writes_.size() == 1) write();
}
void Connection::write() {
    if (closed_ || writes_.empty()) return;
    ws_.text(true);
    ws_.async_write(boost::asio::buffer(writes_.front()), [self = shared_from_this()](beast::error_code ec, std::size_t) {
        if (ec) { self->close(); return; }
        self->queued_bytes_ -= self->writes_.front().size(); self->writes_.pop_front();
        if (self->writes_.empty() && self->close_after_write_) self->close(); else self->write();
    });
}
void Connection::close() {
    if (closed_) return; closed_ = true;
    beast::error_code ignored; beast::get_lowest_layer(ws_).socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    beast::get_lowest_layer(ws_).socket().close(ignored);
    if (joined_) { joined_ = false; hub_.leave(id_, ticket_->sid); }
}
}
