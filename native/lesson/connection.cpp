#include "connection.hpp"
#include "admission.hpp"
#include "assets.hpp"
#include "console_guard.hpp"
#include "http_query.hpp"
#include "prep_sheet.hpp"
#include <boost/asio/post.hpp>

namespace odeum::lesson {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace {
constexpr unsigned max_messages_per_second = 120;   // same ceiling as the relay
constexpr std::size_t max_queued_messages = 256, max_queued_bytes = 512 * 1024;
// Snapshots and timelines for a full class exceed the 16 KiB cap that applies to what clients send.
constexpr std::size_t max_outgoing_bytes = 256 * 1024;

// Tools post one small JSON object; nothing else on this service takes a request body.
constexpr std::uint64_t max_request_body = 4096;

unsigned http_status(const std::string& code) {
    if (code == "tool_unlinked") return 401;
    if (code == "forbidden_role" || code == "ejected") return 403;
    if (code == "class_not_found" || code == "unknown_plot" || code == "link_not_found") return 404;
    if (code == "not_assigned") return 409;
    if (code == "rate_limited") return 429;
    return 400;
}
std::string_view bearer(std::string_view authorization) {
    constexpr std::string_view scheme = "Bearer ";
    return authorization.size() > scheme.size() && beast::iequals(authorization.substr(0, scheme.size()), scheme)
        ? authorization.substr(scheme.size()) : std::string_view();
}
bool student_page(std::string_view path) { return path == "/lesson" || path.starts_with("/web/"); }
bool console_page(std::string_view path) { return path == "/console" || path.starts_with("/web/"); }
bool forwarded(const http::request<http::string_body>& request) {
    for (const auto& header : request) {
        const auto name = header.name_string();
        if (beast::iequals(name, "forwarded") || beast::iequals(name, "x-real-ip") || beast::iequals(name, "cf-connecting-ip")
            || (name.size() > 12 && beast::iequals(name.substr(0, 12), "x-forwarded-"))) return true;
    }
    return false;
}
std::string optional_query(const std::map<std::string, std::string>& query, const char* key) {
    const auto found = query.find(key);
    return found == query.end() ? std::string() : found->second;
}
}

Connection::Connection(boost::asio::ip::tcp::socket socket, Server& server, bool console, std::shared_ptr<std::atomic<std::size_t>> connections)
    : ws_(std::move(socket)), server_(server), console_(console), connections_(std::move(connections)) {
    beast::error_code error;
    const auto peer = beast::get_lowest_layer(ws_).socket().remote_endpoint(error);
    peer_loopback_ = !error && peer.address().is_loopback();
}
Connection::~Connection() { --*connections_; }
void Connection::start() {
    parser_.body_limit(max_request_body); parser_.header_limit(16384);
    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));
    http::async_read(ws_.next_layer(), buffer_, parser_, [self = shared_from_this()](beast::error_code error, std::size_t) {
        if (error) { self->close(); return; }
        self->route();
    });
}
void Connection::respond(unsigned status, const Json& body) {
    auto result = std::make_shared<http::response<http::string_body>>(static_cast<http::status>(status), 11);
    result->set(http::field::content_type, "application/json; charset=utf-8");
    result->set(http::field::cache_control, "no-store"); result->keep_alive(false);
    result->body() = body.dump(); result->prepare_payload();
    http::async_write(ws_.next_layer(), *result, [self = shared_from_this(), result](beast::error_code, std::size_t) { self->close(); });
}
void Connection::respond_bytes(std::string body, std::string_view content_type, std::string_view disposition, bool page) {
    auto result = std::make_shared<http::response<http::string_body>>(http::status::ok, 11);
    result->set(http::field::content_type, content_type);
    result->set(http::field::cache_control, page ? "no-cache" : "no-store");
    result->set("X-Content-Type-Options", "nosniff");
    result->set("Referrer-Policy", "no-referrer");
    if (!disposition.empty()) result->set(http::field::content_disposition, disposition);
    if (page) {
        result->set("Content-Security-Policy",
            "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self' ws: wss:; img-src 'self' data:; base-uri 'none'; form-action 'none'");
        result->set("Permissions-Policy", "microphone=(self), camera=(), geolocation=()");
    }
    result->keep_alive(false); result->body() = std::move(body); result->prepare_payload();
    http::async_write(ws_.next_layer(), *result, [self = shared_from_this(), result](beast::error_code, std::size_t) { self->close(); });
}
// Routes both listeners share: health and the prep sheet download.
bool Connection::serve_shared(const Request&, const std::string& path, const std::map<std::string, std::string>& query) {
    if (path == "/health" && query.empty()) { respond(200, {{"ok", true}, {"version", "0.1.0"}}); return true; }
    if (path == "/v1/prep.pptx") {
        const auto plot = server_.school().plot(optional_query(query, "plot"));
        if (!plot || query.size() != 1) throw ProtocolError("unknown_plot", "Unknown course");
        const auto model = make_provisional_model();
        respond_bytes(write_pptx(build_prep_sheet(*plot, build_timeline(*plot, *model))),
            "application/vnd.openxmlformats-officedocument.presentationml.presentation",
            "attachment; filename=\"" + plot->id + "-prep.pptx\"", false);
        return true;
    }
    return false;
}
void Connection::route() {
    try {
        const auto& request = parser_.get();
        // POST exists only for tools, and only on the students' listener (see route_tool).
        const bool tool_post = !console_ && request.method() == http::verb::post && request.target().starts_with("/v1/tool/");
        if (request.method() != http::verb::get && !tool_post) { respond(405, {{"error", "method_not_allowed"}}); return; }
        if (console_) route_console(request); else route_students(request);
    } catch (const ProtocolError& error) { respond(http_status(error.code), {{"error", error.code}}); }
    catch (const std::exception&) { respond(400, {{"error", "invalid_request"}}); }
}
void Connection::route_students(const Request& request) {
    const std::string authority(request[http::field::host]), origin(request[http::field::origin]);
    // Everything on this listener is served by this service to its own pages: same-origin only.
    if (!server_.config().web_access.allows_same_origin(authority, origin)) { respond(403, {{"error", "web_access_denied"}}); return; }
    const auto target = relay::parse_target(std::string(request.target()));
    if (!target) { respond(400, {{"error", "invalid_request"}}); return; }
    if (target->path.starts_with("/v1/tool/")) { route_tool(request, target->path); return; }
    if (serve_shared(request, target->path, target->query)) return;
    if (student_page(target->path)) {
        if (const auto asset = find_asset(target->path)) { respond_bytes(std::string(asset->body), std::string(asset->content_type) + "; charset=utf-8", {}, true); return; }
    }
    if (target->path != "/v1/join") { respond(404, {{"error", "not_found"}}); return; }
    const auto code = target->query.find("code"), name = target->query.find("name");
    if (code == target->query.end() || name == target->query.end()) throw ProtocolError("invalid_request", "Join code and name required");
    // Resolve after the upgrade so the page can show why it was refused.
    upgrade(request, [this, code = code->second, name = name->second, resume = optional_query(target->query, "resume")] {
        const auto now = monotonic_ms();
        auto& school = server_.school();
        class_ = school.resolve(code, now);
        auto& classroom = school.classroom(class_);
        // The relay's guest rule: 1-32 scalars, trimmed, no control characters.
        const auto admission = classroom.admit(relay::guest_ticket(class_, name).name, resume);
        student_ = admission.student;
        server_.enter(class_, student_, this);
        joined_ = true;
        auto welcome = classroom.connect(student_, now);
        welcome.front().body["stt"] = server_.config().browser_stt ? "browser" : "none";
        server_.deliver(class_, welcome);
    });
}
// A tool on a participant's machine: plain request/response, so that a client with nothing but an
// HTTP stack can take part. The link code and the token are the credentials; neither is logged.
void Connection::route_tool(const Request& request, const std::string& path) {
    const bool post = request.method() == http::verb::post;
    const auto token = bearer(request[http::field::authorization]);
    if (path == "/v1/tool/state" && !post) { respond(200, server_.tool_state(token)); return; }
    if (!post || (path != "/v1/tool/pair" && path != "/v1/tool/progress")) { respond(404, {{"error", "not_found"}}); return; }
    // A JSON content type cannot be sent cross-site without a preflight this service never grants.
    if (!beast::iequals(request[http::field::content_type].substr(0, 16), "application/json")) throw ProtocolError("invalid_request", "JSON required");
    utf8_length(request.body());
    const auto body = Json::parse(request.body());
    respond(200, path == "/v1/tool/pair" ? server_.tool_pair(body) : server_.tool_progress(token, body));
}
void Connection::route_console(const Request& request) {
    ConsoleRequest guard;
    guard.peer_loopback = peer_loopback_;
    guard.forwarded = forwarded(request);
    guard.host = std::string(request[http::field::host]);
    guard.origin = std::string(request[http::field::origin]);
    guard.fetch_site = std::string(request["Sec-Fetch-Site"]);
    if (const auto decision = judge_console(guard, server_.config().console_port); decision != ConsoleDecision::allow) {
        respond(403, {{"error", "console_denied"}, {"reason", console_decision_name(decision)}});
        return;
    }
    const auto target = relay::parse_target(std::string(request.target()));
    if (!target) { respond(400, {{"error", "invalid_request"}}); return; }
    if (serve_shared(request, target->path, target->query)) return;
    if (console_page(target->path)) {
        if (const auto asset = find_asset(target->path)) { respond_bytes(std::string(asset->body), std::string(asset->content_type) + "; charset=utf-8", {}, true); return; }
    }
    if (target->path != "/v1/console") { respond(404, {{"error", "not_found"}}); return; }
    if (!server_.console_token_matches(optional_query(target->query, "token"))) { respond(401, {{"error", "console_token_required"}}); return; }
    upgrade(request, [this] { server_.console_enter(this); joined_ = true; });
}
void Connection::upgrade(const Request& request, std::function<void()> admit) {
    if (!websocket::is_upgrade(request)) { respond(400, {{"error", "upgrade_required"}}); return; }
    beast::get_lowest_layer(ws_).expires_never();
    auto timeout = websocket::stream_base::timeout::suggested(beast::role_type::server);
    timeout.idle_timeout = std::chrono::seconds(60); timeout.keep_alive_pings = true;
    ws_.set_option(timeout); ws_.read_message_max(max_message_bytes);
    ws_.async_accept(request, [self = shared_from_this(), admit = std::move(admit)](beast::error_code error) {
        if (error) { self->close(); return; }
        try { admit(); self->read(); }
        catch (const ProtocolError& failure) { self->close_after_write_ = true; self->send(error_message(failure.code, failure.what())); }
        catch (const std::exception&) { self->close(); }
    });
}
void Connection::read() {
    if (closed_) return;
    ws_.async_read(buffer_, [self = shared_from_this()](beast::error_code error, std::size_t) {
        if (error) { self->close(); return; }
        try {
            if (!self->joined_) { self->close(); return; }
            if (!self->ws_.got_text()) throw ProtocolError("invalid_message", "Text frames required");
            const auto now = monotonic_ms();
            if (now - self->window_ >= 1000) { self->window_ = now; self->messages_ = 0; }
            if (++self->messages_ > max_messages_per_second) { self->close(); return; }
            const auto wire = beast::buffers_to_string(self->buffer_.data());
            utf8_length(wire);
            const auto message = Json::parse(wire);
            if (!message.is_object()) throw ProtocolError("invalid_message", "Object required");
            if (self->console_) self->class_ = self->server_.from_console(self.get(), self->class_, message);
            else self->server_.from_student(self->class_, self->student_, message);
        } catch (const ProtocolError& failure) { self->send(error_message(failure.code, failure.what())); }
        catch (const std::exception&) { self->send(error_message("invalid_message", "Message rejected")); }
        self->buffer_.consume(self->buffer_.size());
        self->read();
    });
}
void Connection::send(const Json& body) {
    if (closed_) return;
    auto wire = body.dump(-1, ' ', false, Json::error_handler_t::replace);
    if (wire.size() > max_outgoing_bytes || queued_bytes_ + wire.size() > max_queued_bytes || writes_.size() >= max_queued_messages) {
        boost::asio::post(ws_.get_executor(), [self = shared_from_this()] { self->close(); });
        return;
    }
    queued_bytes_ += wire.size();
    writes_.push_back(std::move(wire));
    if (writes_.size() == 1) write();
}
void Connection::finish() {
    if (closed_) return;
    close_after_write_ = true;
    if (writes_.empty()) boost::asio::post(ws_.get_executor(), [self = shared_from_this()] { self->close(); });
}
void Connection::write() {
    if (closed_ || writes_.empty()) return;
    ws_.text(true);
    ws_.async_write(boost::asio::buffer(writes_.front()), [self = shared_from_this()](beast::error_code error, std::size_t) {
        if (error) { self->close(); return; }
        self->queued_bytes_ -= self->writes_.front().size();
        self->writes_.pop_front();
        if (self->writes_.empty() && self->close_after_write_) self->close(); else self->write();
    });
}
void Connection::close() {
    if (closed_) return;
    closed_ = true;
    beast::error_code ignored;
    beast::get_lowest_layer(ws_).socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    beast::get_lowest_layer(ws_).socket().close(ignored);
    if (!joined_) return;
    joined_ = false;
    if (console_) server_.console_leave(this); else server_.leave(class_, student_, this);
}
}
