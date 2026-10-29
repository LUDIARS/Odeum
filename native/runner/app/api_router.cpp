#include "api_router.hpp"
#include "../adapters/log.hpp"
#include "../adapters/paths.hpp"
#include "../adapters/playlog_files.hpp"
#include "../core/app_view.hpp"
#include "web_assets.hpp"

namespace odeum::runner {
namespace {
constexpr std::size_t max_gaze_asset_bytes = 64u * 1024 * 1024;

bool prefixed(const std::vector<std::string>& s, std::initializer_list<const char*> head) {
    if (s.size() < head.size()) return false;
    std::size_t i = 0;
    for (const auto* h : head) if (s[i++] != h) return false;
    return true;
}

ServerResponse unauthorized() { return ServerResponse::json(401, Json{{"error", "unauthorized"}}); }

std::optional<Json> json_body(const ServerRequest& request) { return try_parse_json(request.body, http_body_limits); }

std::string error_text(const std::exception& error) { return error.what(); }
}

void ApiRouter::authenticated(const ServerRequest& request, Reply reply, std::function<void(const VerifiedUser&)> next) {
    const auto token = bearer_token(request.header("authorization").value_or(""));
    deps_.auth.verify(token, [reply, next = std::move(next)](std::optional<VerifiedUser> user) {
        if (!user) return reply(unauthorized());
        next(*user);
    });
}

void ApiRouter::handle(const ServerRequest& request, Reply reply) {
    const auto segments = path_segments(request.path);
    if (!segments) return reply(ServerResponse::text(400, "Bad Request"));
    const auto& s = *segments;
    const bool get = request.method == "GET" || request.method == "HEAD";

    if (get && request.path == "/api/health") {
        return reply(ServerResponse::json(200, Json{{"status", "ok"}, {"service", "custos"}, {"version", "0.1.0"}, {"apps", deps_.registry.configs().size()}}));
    }
    if (get && request.path == "/api/video/encoder") {
        const bool forced = deps_.encoder_env_set;
        return deps_.media.encoder([reply, forced](Encoder e) { reply(ServerResponse::json(200, Json{{"encoder", encoder_name(e)}, {"forced", forced}})); });
    }
    auto guarded = [&](std::function<bool(const std::vector<std::string>&)> route) {
        authenticated(request, reply, [this, request, reply, s, route](const VerifiedUser&) {
            if (!route(s)) serve_static(request, reply);
        });
    };
    if (prefixed(s, {"api", "apps"})) return guarded([this, request, reply](const std::vector<std::string>& seg) { return apps(request, seg, reply); });
    if (prefixed(s, {"api", "settings"})) return guarded([this, request, reply](const std::vector<std::string>& seg) { return settings(request, seg, reply); });
    if (prefixed(s, {"api", "unity"})) {
        return guarded([this, request, reply](const std::vector<std::string>& seg) {
            return seg.size() == 4 && deps_.unity.handle(request, seg[2], seg[3], reply);
        });
    }
    if (prefixed(s, {"api", "playlogs"})) return guarded([this, request, reply](const std::vector<std::string>& seg) { return playlogs(request, seg, reply); });
    if (deps_.farm && prefixed(s, {"api", "farm"}) && farm(request, s, reply)) return;
    serve_static(request, reply);
}

bool ApiRouter::apps(const ServerRequest& request, const std::vector<std::string>& s, Reply reply) {
    const bool get = request.method == "GET" || request.method == "HEAD";
    const bool post = request.method == "POST";
    // /api/apps
    if (get && (s.size() == 2 || (s.size() == 3 && s[2].empty()))) {
        Json items = Json::array();
        for (const auto& app : deps_.registry.configs()) items.push_back(Json{{"config", redact(app)}, {"status", to_json(*deps_.registry.status(app.id))}});
        reply(ServerResponse::json(200, Json{{"apps", items}}));
        return true;
    }
    // /api/apps/_/rtc/ice-servers | diagnostic
    if (get && s.size() == 5 && s[2] == "_" && s[3] == "rtc") {
        if (s[4] == "ice-servers") {
            deps_.ice.get([reply](std::vector<IceServer> servers) { reply(ServerResponse::json(200, Json{{"iceServers", to_json(servers)}})); });
            return true;
        }
        if (s[4] == "diagnostic") {
            reply(ServerResponse::json(200, Json{{"sessions", deps_.rtc ? deps_.rtc->diagnostics() : Json::array()}}));
            return true;
        }
    }
    if (s.size() < 3) return false;
    const auto& id = s[2];
    const auto* config = deps_.registry.config(id);
    auto unknown = [&] { reply(ServerResponse::json(404, Json{{"error", "unknown app"}})); };
    if (get && s.size() == 3) {
        if (!config) { unknown(); return true; }
        reply(ServerResponse::json(200, Json{{"config", redact(*config)}, {"status", to_json(*deps_.registry.status(id))}}));
        return true;
    }
    if (get && s.size() == 4 && s[3] == "status") {
        const auto* status = deps_.registry.status(id);
        if (!status) { unknown(); return true; }
        reply(ServerResponse::json(200, to_json(*status)));
        return true;
    }
    if (!post) return false;
    if (s.size() == 4 && (s[3] == "build" || s[3] == "run" || s[3] == "test")) {
        if (!config) { unknown(); return true; }
        try {
            if (s[3] == "build") reply(ServerResponse::json(200, Json{{"ok", true}, {"buildId", deps_.runner.start_build(*config)}}));
            else if (s[3] == "test") reply(ServerResponse::json(200, Json{{"ok", true}, {"testId", deps_.runner.start_test(*config)}}));
            else {
                const auto run_id = deps_.runner.start_run(*config);
                const auto pid = deps_.runner.run_pid(id);
                reply(ServerResponse::json(200, Json{{"ok", true}, {"runId", run_id}, {"pid", pid ? Json(*pid) : Json(nullptr)}}));
            }
        } catch (const std::exception& error) {
            reply(ServerResponse::json(409, Json{{"ok", false}, {"error", error_text(error)}}));
        }
        return true;
    }
    if (s.size() == 4 && s[3] == "kill") {
        if (!config) { unknown(); return true; }
        reply(ServerResponse::json(200, Json{{"ok", deps_.runner.kill(id)}}));
        return true;
    }
    if (s.size() == 4 && s[3] == "screenshot") {
        if (!config) { unknown(); return true; }
        if (!config->in_app_bridge && !config->capture) {
            reply(ServerResponse::json(400, Json{{"error", "app has neither inAppBridge nor capture config"}}));
            return true;
        }
        deps_.screenshots.capture(*config, [reply](PngResult result) {
            if (!png_ok(result)) return reply(ServerResponse::json(500, Json{{"ok", false}, {"error", std::get<1>(result)}}));
            ServerResponse response{200, "image/png", std::move(std::get<0>(result)), {}};
            reply(std::move(response));
        });
        return true;
    }
    if (s.size() == 5 && s[3] == "rtc" && s[4] == "offer") {
        if (!config) { unknown(); return true; }
        if (!config->capture && !config->in_app_bridge) {
            reply(ServerResponse::json(400, Json{{"error", "app has neither capture nor inAppBridge config"}}));
            return true;
        }
        const auto body = json_body(request);
        if (!body) { reply(ServerResponse::text(500, "Internal Server Error")); return true; }
        const auto sdp = body->is_object() && body->contains("sdp") && (*body)["sdp"].is_string() ? (*body)["sdp"].get<std::string>() : std::string();
        if (sdp.empty()) { reply(ServerResponse::json(400, Json{{"error", "sdp required"}})); return true; }
        if (!deps_.rtc) { reply(ServerResponse::json(500, Json{{"ok", false}, {"error", "WebRTC is not available in this build"}})); return true; }
        deps_.rtc->create_session(*config, sdp, [reply](std::variant<RtcAnswer, std::string> result) {
            if (auto* message = std::get_if<std::string>(&result)) return reply(ServerResponse::json(500, Json{{"ok", false}, {"error", *message}}));
            const auto& answer = std::get<RtcAnswer>(result);
            Json out{{"ok", true}, {"sessionId", answer.session_id}, {"sdp", answer.sdp}, {"type", answer.type}};
            if (answer.audio_error) out["audioError"] = *answer.audio_error;
            reply(ServerResponse::json(200, out));
        });
        return true;
    }
    if (s.size() == 5 && s[3] == "rtc" && s[4] == "close") {
        const auto body = json_body(request);
        if (!body) { reply(ServerResponse::text(500, "Internal Server Error")); return true; }
        const auto session = body->is_object() && body->contains("sessionId") && (*body)["sessionId"].is_string() ? (*body)["sessionId"].get<std::string>() : std::string();
        if (session.empty()) { reply(ServerResponse::json(400, Json{{"error", "sessionId required"}})); return true; }
        reply(ServerResponse::json(200, Json{{"ok", deps_.rtc && deps_.rtc->close_session(session)}}));
        return true;
    }
    return false;
}

bool ApiRouter::settings(const ServerRequest& request, const std::vector<std::string>& s, Reply reply) {
    if (s.size() != 3 || s[2] != "stream") return false;
    if (request.method == "GET" || request.method == "HEAD") {
        reply(ServerResponse::json(200, Json{{"ok", true}, {"prefs", deps_.prefs.to_json()}}));
        return true;
    }
    if (request.method != "PUT") return false;
    const auto body = json_body(request);
    if (!body || body->is_null() || !(body->is_object() || body->is_array())) {
        reply(ServerResponse::json(400, Json{{"ok", false}, {"error", "invalid JSON"}}));
        return true;
    }
    auto patch = parse_stream_prefs_patch(*body);
    if (auto* message = std::get_if<std::string>(&patch)) {
        reply(ServerResponse::json(400, Json{{"ok", false}, {"error", *message}}));
        return true;
    }
    deps_.prefs.apply(std::get<StreamPrefsPatch>(patch));
    reply(ServerResponse::json(200, Json{{"ok", true}, {"prefs", deps_.prefs.to_json()}}));
    return true;
}

bool ApiRouter::playlogs(const ServerRequest& request, const std::vector<std::string>& s, Reply reply) {
    if (request.method != "GET" && request.method != "HEAD") return false;
    if (s.size() == 2 || (s.size() == 3 && s[2].empty())) {
        std::optional<std::string> app_id;
        if (auto it = request.query.find("appId"); it != request.query.end() && !it->second.empty()) app_id = it->second;
        Json logs = Json::array();
        for (const auto& entry : list_play_logs(deps_.playlog_dir, app_id)) logs.push_back(to_json(entry));
        reply(ServerResponse::json(200, Json{{"logs", logs}}));
        return true;
    }
    if (s.size() == 4) {
        const auto body = read_play_log(deps_.playlog_dir, s[2], s[3]);
        if (!body) reply(ServerResponse::json(404, Json{{"error", "not found"}}));
        else reply(ServerResponse{200, "application/x-ndjson; charset=utf-8", *body, {}});
        return true;
    }
    return false;
}

bool ApiRouter::farm(const ServerRequest& request, const std::vector<std::string>& s, Reply reply) {
    auto& farm = *deps_.farm;
    // Disk paths, bearer tokens and request bodies are not part of diagnostics.
    auto guard = [reply](const std::function<void()>& action) {
        try {
            action();
        } catch (const FarmError& error) {
            reply(ServerResponse::json(error.status(), Json{{"error", error.code()}}));
        } catch (const std::exception&) {
            log::error("device-farm", "device farm operation failed");
            reply(ServerResponse::json(503, Json{{"error", "farm_storage_unavailable"}}));
        }
    };
    auto body = [text = request.body]() {
        auto parsed = try_parse_json(text, http_body_limits);
        if (!parsed) throw FarmError("invalid_body", 400);
        return *parsed;
    };
    if (request.method == "POST" && s.size() == 5 && s[2] == "hosts" && s[4] == "report") {
        const auto host_id = s[3];
        if (!farm.host_auth->verify(host_id, request.header("authorization").value_or(""))) {
            reply(ServerResponse::json(401, Json{{"error", "unauthorized_host"}}));
            return true;
        }
        guard([&] { reply(ServerResponse::json(200, Json{{"devices", farm.pool->report(host_id, parse_host_report(body()))}})); });
        return true;
    }
    if (s.size() < 3 || s[2] != "devices") return false;
    const bool list = (request.method == "GET" || request.method == "HEAD") && s.size() == 3;
    const bool lease = request.method == "POST" && s.size() == 5 && s[4] == "lease";
    const bool lease_op = request.method == "POST" && s.size() == 6 && s[4] == "lease" && (s[5] == "renew" || s[5] == "release");
    authenticated(request, reply, [this, request, reply, s, list, lease, lease_op, guard, body](const VerifiedUser& user) {
        auto& farm = *deps_.farm;
        if (list) return guard([&] { reply(ServerResponse::json(200, Json{{"devices", farm.pool->list()}})); });
        if (lease) return guard([&] { auto ttl = parse_acquire(body()); reply(ServerResponse::json(201, farm.pool->acquire(s[3], user.id, ttl))); });
        if (lease_op && s[5] == "renew") return guard([&] { auto [id, ttl] = parse_renew(body()); reply(ServerResponse::json(200, farm.pool->renew(s[3], user.id, id, ttl))); });
        if (lease_op) return guard([&] { auto id = parse_release(body()); reply(ServerResponse::json(200, farm.pool->release(s[3], user.id, id))); });
        serve_static(request, reply);
    });
    return true;
}

void ApiRouter::serve_static(const ServerRequest& request, Reply reply) {
    if (request.method != "GET" && request.method != "HEAD") return reply(ServerResponse::not_found());
    if (const auto* asset = find_web_asset(request.path)) {
        return reply(ServerResponse{200, std::string(asset->content_type), std::string(asset->body), {}});
    }
    if (const auto gaze = gaze_asset(request.path)) {
        if (auto data = read_file_limited(from_path(to_path(deps_.gaze_assets_dir) / to_path(gaze->relative)), max_gaze_asset_bytes)) {
            return reply(ServerResponse{200, gaze->content_type, std::move(*data), {}});
        }
    }
    reply(ServerResponse::not_found());
}
}
