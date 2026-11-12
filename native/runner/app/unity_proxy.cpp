#include "unity_proxy.hpp"
#include <cmath>
#include <cstdlib>
#include <limits>

namespace odeum::runner {
double js_to_number(std::string_view raw) {
    std::string text(raw);
    const auto begin = text.find_first_not_of(" \t\r\n\v\f");
    if (begin == std::string::npos) return 0;
    text = text.substr(begin, text.find_last_not_of(" \t\r\n\v\f") - begin + 1);
    if (text == "Infinity" || text == "+Infinity") return std::numeric_limits<double>::infinity();
    if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        char* end = nullptr;
        const auto v = std::strtoull(text.c_str() + 2, &end, 16);
        return end && *end == '\0' ? static_cast<double>(v) : std::nan("");
    }
    for (char c : text) if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')) return std::nan("");
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    return end && *end == '\0' ? v : std::nan("");
}

namespace {
std::string query_number(double v) { return js_number(v).dump(); }
}

UnityProxy::UnityProxy(const AppsRegistry& registry, ControlLease& lease, ClientFactory factory)
    : registry_(registry), lease_(lease), factory_(std::move(factory)) {}

void UnityProxy::proxy(const std::string& app_id, Call call, Reply reply) {
    const auto target = resolve_bridge_target(registry_.configs(), app_id);
    if (!target) {
        return reply(ServerResponse::json(404, Json{{"error", "unknown app or no unity bridge configured"}, {"appId", app_id.empty() ? Json(nullptr) : Json(app_id)}}));
    }
    auto client = factory_(*target);
    call(*client, [client, reply](UnityResult result) {
        if (auto* data = std::get_if<Json>(&result)) return reply(ServerResponse::json(200, Json{{"bridge", "up"}, {"data", *data}}));
        const auto& error = std::get<UnityBridgeError>(result);
        switch (error.kind) {
            case UnityBridgeError::Kind::unreachable:
                return reply(ServerResponse::json(200, Json{{"bridge", "down"}, {"error", error.message}}));
            case UnityBridgeError::Kind::http:
                // 504: the bridge waits for Unity's main thread; it is alive, so "busy".
                return reply(ServerResponse::json(error.status, Json{{"bridge", error.status == 504 ? "busy" : "up"}, {"error", error.body}}));
            case UnityBridgeError::Kind::invalid_json:
                return reply(ServerResponse::text(500, "Internal Server Error"));
        }
    });
}

bool UnityProxy::handle(const ServerRequest& request, const std::string& app_id, const std::string& action, Reply reply) {
    const bool get = request.method == "GET" || request.method == "HEAD";
    const bool post = request.method == "POST";
    auto query = [&](const char* name) -> std::optional<std::string> {
        auto it = request.query.find(name);
        if (it == request.query.end()) return std::nullopt;
        return it->second;
    };
    if (get && action == "health") { proxy(app_id, [](UnityBridgeClient& c, auto done) { c.health(std::move(done)); }, reply); return true; }
    if (get && action == "status") { proxy(app_id, [](UnityBridgeClient& c, auto done) { c.status(std::move(done)); }, reply); return true; }
    if (get && action == "compile-status") { proxy(app_id, [](UnityBridgeClient& c, auto done) { c.compile_status(std::move(done)); }, reply); return true; }
    if (get && action == "scene") { proxy(app_id, [](UnityBridgeClient& c, auto done) { c.scene(std::move(done)); }, reply); return true; }
    if (get && action == "hierarchy") {
        const double depth = js_to_number(query("depth").value_or("2"));
        const auto root = query("root").value_or("");
        const auto depth_text = std::isfinite(depth) ? query_number(depth) : std::string("2");
        proxy(app_id, [depth_text, root](UnityBridgeClient& c, auto done) {
            std::vector<std::pair<std::string, std::string>> q{{"depth", depth_text}};
            if (!root.empty()) q.emplace_back("root", root);
            c.get("/editor/hierarchy?" + form_encode(q), std::move(done));
        }, reply);
        return true;
    }
    if (get && action == "log") {
        const double since = js_to_number(query("since").value_or("0"));
        const double limit = js_to_number(query("limit").value_or("200"));
        const auto level = query("level").value_or("");
        const auto since_text = std::isfinite(since) ? query_number(since) : std::string("0");
        const auto limit_text = std::isfinite(limit) ? query_number(limit) : std::string("200");
        proxy(app_id, [since_text, limit_text, level](UnityBridgeClient& c, auto done) {
            std::vector<std::pair<std::string, std::string>> q{{"since", since_text}, {"limit", limit_text}};
            if (!level.empty()) q.emplace_back("level", level);
            c.get("/editor/log?" + form_encode(q), std::move(done));
        }, reply);
        return true;
    }
    if (!post || (action != "compile" && action != "refresh" && action != "play" && action != "stop" && action != "publish-capture")) return false;

    // Operations are refused for spectators (another connection holds the app).
    const auto client_id = request.header("x-custos-client");
    if (!lease_.may_operate(app_id, client_id)) {
        reply(ServerResponse::json(409, Json{{"ok", false}, {"error", "spectating: another user is playing this app"}}));
        return true;
    }
    auto wrap = [](Json data) {
        return [data](UnityResult result) -> UnityResult { return std::holds_alternative<Json>(result) ? UnityResult(data) : result; };
    };
    if (action == "compile") {
        proxy(app_id, [wrap](UnityBridgeClient& c, auto done) { c.request_compile([done, w = wrap(Json{{"requested", true}})](UnityResult r) { done(w(std::move(r))); }); }, reply);
    } else if (action == "refresh") {
        proxy(app_id, [wrap](UnityBridgeClient& c, auto done) { c.refresh([done, w = wrap(Json{{"refreshed", true}})](UnityResult r) { done(w(std::move(r))); }); }, reply);
    } else if (action == "play") {
        proxy(app_id, [](UnityBridgeClient& c, auto done) { c.play(std::move(done)); }, reply);
    } else if (action == "stop") {
        proxy(app_id, [](UnityBridgeClient& c, auto done) { c.stop(std::move(done)); }, reply);
    } else {
        const auto body = try_parse_json(request.body, http_body_limits);
        Json caption = "", source = "gameview";
        if (body && body->is_object()) {
            if (auto it = body->find("caption"); it != body->end() && !it->is_null()) caption = *it;
            if (auto it = body->find("source"); it != body->end() && !it->is_null()) source = *it;
        }
        proxy(app_id, [caption, source](UnityBridgeClient& c, auto done) {
            c.post("/editor/publish-capture", Json{{"caption", caption}, {"source", source}}, std::move(done));
        }, reply);
    }
    return true;
}
}
