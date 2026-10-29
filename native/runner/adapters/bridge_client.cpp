#include "bridge_client.hpp"
#include "log.hpp"

namespace odeum::runner {
namespace {
constexpr std::chrono::milliseconds screenshot_timeout{8'000};
constexpr std::chrono::milliseconds key_timeout{2'000};
constexpr std::size_t small_body = 64 * 1024;

std::string snippet(const std::string& text) { return text.substr(0, 200); }

HttpRequest post_json(const BridgeEndpoint& ep, const std::string& path, const std::string& body) {
    HttpRequest request;
    request.method = "POST";
    request.url = ep.base_url() + path;
    request.headers = {{"content-type", "application/json"}};
    request.body = body;
    request.timeout = key_timeout;
    request.body_limit = small_body;
    return request;
}

std::string failure(const HttpError& error) { return error.message; }
}

std::string BridgeEndpoint::base_url() const {
    const bool v6 = host.find(':') != std::string::npos;
    return "http://" + (v6 ? "[" + host + "]" : host) + ":" + std::to_string(port);
}

void bridge_fetch_screenshot(boost::asio::io_context& io, const BridgeEndpoint& ep, std::function<void(std::variant<std::string, BridgeCallError>)> done) {
    HttpRequest request;
    request.url = ep.base_url() + "/screenshot";
    request.timeout = screenshot_timeout;
    request.body_limit = max_bridge_png_bytes;
    http_request(io, std::move(request), [done = std::move(done)](HttpResult result) {
        if (auto* error = std::get_if<HttpError>(&result)) return done(BridgeCallError{BridgeCallError::Kind::failed, failure(*error)});
        auto& response = std::get<HttpResponse>(result);
        if (!response.ok()) return done(BridgeCallError{BridgeCallError::Kind::failed, "in-app bridge /screenshot " + std::to_string(response.status) + ": " + snippet(response.body)});
        if (!looks_like_png(response.body)) return done(BridgeCallError{BridgeCallError::Kind::failed, "in-app bridge /screenshot did not return a PNG"});
        done(std::move(response.body));
    });
}

void bridge_send_key(boost::asio::io_context& io, const BridgeEndpoint& ep, int code, bool down, std::function<void(std::optional<BridgeCallError>)> done) {
    http_request(io, post_json(ep, "/key", dump(Json{{"code", code}, {"down", down}})), [done = std::move(done)](HttpResult result) {
        if (auto* error = std::get_if<HttpError>(&result)) return done(BridgeCallError{BridgeCallError::Kind::failed, failure(*error)});
        const auto& response = std::get<HttpResponse>(result);
        if (!response.ok() && response.status != 204) return done(BridgeCallError{BridgeCallError::Kind::failed, "in-app bridge /key " + std::to_string(response.status) + ": " + snippet(response.body)});
        done(std::nullopt);
    });
}

void bridge_send_pointer(boost::asio::io_context& io, const BridgeEndpoint& ep, double x, double y, const std::string& button,
    const std::string& phase, std::function<void(std::optional<BridgeCallError>)> done) {
    const int code = button == "right" ? 1 : button == "middle" ? 2 : 0;
    http_request(io, post_json(ep, "/pointer", dump(Json{{"x", js_number(x)}, {"y", js_number(y)}, {"button", code}, {"phase", phase}})),
        [done = std::move(done)](HttpResult result) {
            if (auto* error = std::get_if<HttpError>(&result)) return done(BridgeCallError{BridgeCallError::Kind::failed, failure(*error)});
            const auto& response = std::get<HttpResponse>(result);
            if (response.status == 404) return done(BridgeCallError{BridgeCallError::Kind::unsupported, "in-app bridge does not support /pointer"});
            if (!response.ok() && response.status != 204) return done(BridgeCallError{BridgeCallError::Kind::failed, "in-app bridge /pointer " + std::to_string(response.status) + ": " + snippet(response.body)});
            done(std::nullopt);
        });
}

void bridge_fetch_state(boost::asio::io_context& io, const BridgeEndpoint& ep, std::function<void(std::variant<BridgeState, BridgeCallError>)> done) {
    HttpRequest request;
    request.url = ep.base_url() + "/state";
    request.timeout = key_timeout;
    request.body_limit = 1024 * 1024;
    http_request(io, std::move(request), [done = std::move(done)](HttpResult result) {
        if (auto* error = std::get_if<HttpError>(&result)) return done(BridgeCallError{BridgeCallError::Kind::failed, failure(*error)});
        const auto& response = std::get<HttpResponse>(result);
        if (response.status == 404) return done(BridgeCallError{BridgeCallError::Kind::unsupported, "in-app bridge does not support /state"});
        if (!response.ok()) return done(BridgeCallError{BridgeCallError::Kind::failed, "in-app bridge /state " + std::to_string(response.status)});
        auto body = try_parse_json(response.body, JsonLimits{1024 * 1024, 65536, 32});
        if (!body) return done(BridgeCallError{BridgeCallError::Kind::failed, "in-app bridge /state returned invalid JSON"});
        done(parse_bridge_state(*body));
    });
}

void bridge_post_control(boost::asio::io_context& io, const BridgeEndpoint& ep, const std::string& path, const std::string& body,
    std::function<void(std::optional<std::string>)> done) {
    http_request(io, post_json(ep, path, body), [done = std::move(done), path](HttpResult result) {
        if (auto* error = std::get_if<HttpError>(&result)) return done(error->kind == HttpFailure::timeout ? "bridge " + path + " timeout" : error->message);
        const auto& response = std::get<HttpResponse>(result);
        if (response.status != 200) return done("bridge " + path + " returned HTTP " + std::to_string(response.status));
        done(std::nullopt);
    });
}

UnityBridgeClient::UnityBridgeClient(boost::asio::io_context& io, BridgeEndpoint endpoint, std::string token, std::chrono::milliseconds timeout)
    : io_(io), endpoint_(std::move(endpoint)), token_(std::move(token)), timeout_(timeout) {}

void UnityBridgeClient::get(const std::string& target, std::function<void(UnityResult)> done) { request("GET", target, std::nullopt, std::move(done)); }

void UnityBridgeClient::post(const std::string& target, const Json& body, std::function<void(UnityResult)> done) { request("POST", target, body, std::move(done)); }

void UnityBridgeClient::log(long long since, const std::string& level, long long limit, std::function<void(UnityResult)> done) {
    std::vector<std::pair<std::string, std::string>> query{{"since", std::to_string(since)}, {"limit", std::to_string(limit)}};
    if (!level.empty()) query.emplace_back("level", level);
    get("/editor/log?" + form_encode(query), std::move(done));
}

void UnityBridgeClient::hierarchy(long long depth, const std::string& root, std::function<void(UnityResult)> done) {
    std::vector<std::pair<std::string, std::string>> query{{"depth", std::to_string(depth)}};
    if (!root.empty()) query.emplace_back("root", root);
    get("/editor/hierarchy?" + form_encode(query), std::move(done));
}

void UnityBridgeClient::publish_capture(const std::string& caption, const std::string& source, std::function<void(UnityResult)> done) {
    post("/editor/publish-capture", Json{{"caption", caption}, {"source", source}}, std::move(done));
}

void UnityBridgeClient::request(const std::string& method, const std::string& target, std::optional<Json> body, std::function<void(UnityResult)> done) {
    HttpRequest request;
    request.method = method;
    request.url = endpoint_.base_url() + target;
    request.headers = {{"accept", "application/json"}};
    if (!token_.empty()) request.headers.emplace_back("x-auth-token", token_);
    if (body) {
        request.headers.emplace_back("content-type", "application/json");
        request.body = dump(*body);
    }
    request.timeout = timeout_;
    request.body_limit = bridge_response_limits.bytes;
    const auto base = endpoint_.base_url();
    http_request(io_, std::move(request), [done = std::move(done), base](HttpResult result) {
        if (auto* error = std::get_if<HttpError>(&result)) {
            return done(UnityBridgeError{UnityBridgeError::Kind::unreachable, 0, "Unity bridge unreachable at " + base + ": " + error->message, {}});
        }
        auto& response = std::get<HttpResponse>(result);
        if (!response.ok()) {
            return done(UnityBridgeError{UnityBridgeError::Kind::http, response.status, "Unity bridge returned " + std::to_string(response.status) + ": " + snippet(response.body), response.body});
        }
        if (response.body.empty()) return done(Json::object());
        auto parsed = try_parse_json(response.body, bridge_response_limits);
        if (!parsed) return done(UnityBridgeError{UnityBridgeError::Kind::invalid_json, response.status, "Unity bridge returned invalid JSON", {}});
        done(std::move(*parsed));
    });
}
}
