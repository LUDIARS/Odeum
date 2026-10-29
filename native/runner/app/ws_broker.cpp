#include "ws_broker.hpp"
#include "../adapters/log.hpp"
#include "../core/client_message.hpp"
#include <openssl/evp.h>

namespace odeum::runner {
std::string base64(std::string_view bytes) {
    std::string out(4 * ((bytes.size() + 2) / 3), '\0');
    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()));
    out.resize(static_cast<std::size_t>(n < 0 ? 0 : n));
    return out;
}

namespace {
// JavaScript truthiness of a JSON value.
bool truthy(const Json& value) {
    if (value.is_null()) return false;
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number()) return value.get<double>() != 0;
    if (value.is_string()) return !value.get_ref<const std::string&>().empty();
    return true;
}

// String(value) for the error texts that quote a client value.
std::string js_text(const Json* value) {
    if (!value) return "undefined";
    if (value->is_string()) return value->get<std::string>();
    if (value->is_null()) return "null";
    if (value->is_object()) return "[object Object]";
    return value->dump();
}
}

WsBroker::WsBroker(WsBrokerDeps deps) : deps_(std::move(deps)) {
    deps_.control.subscribe([this](const std::string& app_id, const std::optional<std::string>&) {
        for (auto& [id, s] : sessions_) {
            if (s.app_ids.contains(app_id)) send(s, Json{{"type", "control"}, {"appId", app_id}, {"role", deps_.control.role_of(app_id, s.id)}});
        }
    });
    deps_.registry.on_status_changed([this](const std::string& app_id, const AppStatus& status) {
        broadcast(app_id, Json{{"type", "status"}, {"appId", app_id}, {"status", to_json(status)}});
    });
    deps_.runner.on_log([this](const std::string& app_id, RunnerKind kind, const char* stream, const std::string& text) {
        broadcast(app_id, Json{{"type", "log"}, {"appId", app_id}, {"kind", to_string(kind)}, {"stream", stream}, {"text", text}, {"ts", deps_.now()}});
    });
    deps_.runner.on_exit([this](const std::string& app_id, RunnerKind kind, const ExitStatus& status) {
        broadcast(app_id, Json{{"type", "exit"}, {"appId", app_id}, {"kind", to_string(kind)},
            {"exitCode", status.code ? Json(*status.code) : Json(nullptr)}, {"signal", status.signal ? Json(*status.signal) : Json(nullptr)}, {"ts", deps_.now()}});
    });
    // Screenshots are JSON with base64 PNG (the browser code reads JSON only). Skippable when slow.
    deps_.streamer.on_frame([this](const std::string& app_id, long long ts, const std::string& png) {
        bool any = false;
        for (const auto& [id, s] : sessions_) any |= s.app_ids.contains(app_id);
        if (!any) return;
        broadcast(app_id, Json{{"type", "screenshot"}, {"appId", app_id}, {"ts", ts}, {"png_b64", base64(png)}, {"mime", "image/png"}, {"bytes", png.size()}}, true);
    });
    deps_.audience.subscribe([this](const AudienceEvent& event) {
        for (auto& [id, s] : sessions_) {
            if (s.audience_app_ids.contains(event.app_id)) send(s, Json{{"type", "audience-event"}, {"appId", event.app_id}, {"event", to_json(event)}});
        }
    });
}

void WsBroker::authorize(const ServerRequest& upgrade, std::function<void(std::optional<VerifiedUser>)> done) {
    // ?token= first (browsers cannot set headers on WebSocket), then Authorization: Bearer.
    std::string token;
    if (auto it = upgrade.query.find("token"); it != upgrade.query.end()) token = it->second;
    else token = bearer_token(upgrade.header("authorization").value_or(""));
    deps_.auth.verify(token, std::move(done));
}

std::string WsBroker::open(std::shared_ptr<WsPeer> peer, const VerifiedUser& user) {
    Session session;
    session.id = deps_.new_id();
    session.peer = std::move(peer);
    session.user = user;
    const auto id = session.id;
    auto& stored = sessions_.emplace(id, std::move(session)).first->second;
    log::info("ws", "ws connected", {{"sessionId", id}});
    send(stored, Json{{"type", "welcome"}, {"clientId", id}});
    return id;
}

void WsBroker::closed(const std::string& session_id) {
    if (!sessions_.contains(session_id)) return;
    deps_.streamer.release_all(session_id);
    if (deps_.play_log) deps_.play_log->close_session(session_id, "disconnect");
    deps_.control.release_all(session_id);
    sessions_.erase(session_id);
    log::info("ws", "ws disconnected", {{"sessionId", session_id}});
}

void WsBroker::send(Session& session, const Json& message, bool droppable) {
    session.peer->send_text(dump(message), droppable);
}

void WsBroker::broadcast(const std::string& app_id, const Json& message, bool droppable) {
    std::string text;
    for (auto& [id, s] : sessions_) {
        if (!s.app_ids.contains(app_id)) continue;
        if (text.empty()) text = dump(message);
        s.peer->send_text(text, droppable);
    }
}

void WsBroker::error(Session& session, const std::string& message) {
    send(session, Json{{"type", "error"}, {"message", message}});
}

void WsBroker::message(const std::string& session_id, std::string_view text) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return;
    auto& session = it->second;
    std::optional<Json> parsed;
    try {
        parsed = parse_json(text, ws_message_limits);
    } catch (const std::exception&) {
        return error(session, "invalid JSON");
    }
    if (!parsed->is_object()) return error(session, "invalid message");
    try {
        handle(session, *parsed);
    } catch (const std::exception& failure) {
        log::warn("ws", "client message handler failed", {{"err", failure.what()}});
        if (auto s = sessions_.find(session_id); s != sessions_.end()) error(s->second, failure.what());
    }
}

void WsBroker::handle(Session& session, const Json& msg) {
    const auto type = string_field(msg, "type").value_or("");
    const auto app_value = msg.find("appId");
    const auto app_id = string_field(msg, "appId").value_or("");
    const bool subscribed = !app_id.empty() && session.app_ids.contains(app_id);

    if (type == "audience-send") {
        const auto submission = parse_audience_submission(msg);
        if (!submission || !session.app_ids.contains(submission->app_id)) return error(session, "invalid audience message or subscription");
        const auto now = deps_.now();
        if (session.last_audience_at && now - *session.last_audience_at < audience_min_interval_ms) return error(session, "少し待ってから送信してください。");
        session.last_audience_at = now;
        const auto sender = is_placeholder_user(session.user.id) ? "観戦者 " + session.id.substr(0, 4)
            : (session.user.name.empty() ? std::string("観戦者") : session.user.name);
        const auto user_id = session.user.id;
        deps_.audience.append(*submission, sender);
        if (submission->kind == "message" && deps_.play_log) deps_.play_log->record_comment(submission->app_id, user_id, submission->text);
        return;
    }
    if (type == "audience-subscribe") {
        if (app_id.empty() || !deps_.registry.config(app_id)) return error(session, "unknown audience app");
        session.audience_app_ids.insert(app_id);
        return send(session, Json{{"type", "audience-history"}, {"appId", app_id}, {"events", deps_.audience.history_json(app_id)}});
    }
    if (type == "subscribe") {
        const auto* config = app_value != msg.end() && app_value->is_string() ? deps_.registry.config(app_id) : nullptr;
        if (!config) return error(session, "unknown appId: " + js_text(app_value != msg.end() ? &*app_value : nullptr));
        session.app_ids.insert(app_id);
        session.audience_app_ids.insert(app_id);
        send(session, Json{{"type", "audience-history"}, {"appId", app_id}, {"events", deps_.audience.history_json(app_id)}});
        if (const auto* status = deps_.registry.status(app_id)) send(session, Json{{"type", "hello"}, {"appId", app_id}, {"status", to_json(*status)}});
        send(session, Json{{"type", "control"}, {"appId", app_id}, {"role", deps_.control.role_of(app_id, session.id)}});
        deps_.streamer.acquire(*config, session.id);
        return;
    }
    if (type == "unsubscribe") {
        session.app_ids.erase(app_id);
        session.audience_app_ids.erase(app_id);
        deps_.streamer.release(app_id, session.id);
        if (deps_.play_log) deps_.play_log->close_session_app(session.id, app_id, "unsubscribe");
        deps_.control.release(app_id, session.id);
        return;
    }
    if (type == "control") {
        if (!subscribed) return;
        const auto want = msg.find("want");
        if (want != msg.end() && truthy(*want)) {
            if (!deps_.control.acquire(app_id, session.id)) send(session, Json{{"type", "control"}, {"appId", app_id}, {"role", "spectator"}});
        } else {
            deps_.control.release(app_id, session.id);
        }
        return;
    }
    if (type == "comment") {
        const auto comment = parse_comment_message(msg);
        if (!comment || !session.app_ids.contains(comment->app_id)) return;
        const auto text = normalize_comment_text(comment->text);
        if (!text) return error(session, "comment must be 1-200 characters");
        const auto now = deps_.now();
        if (!can_post_comment(session.last_comment_at, now)) return;  // bursts are dropped silently
        session.last_comment_at = now;
        const auto user_id = session.user.id;
        broadcast(comment->app_id, Json{{"type", "comment"}, {"appId", comment->app_id}, {"text", *text}, {"ts", now}});
        deps_.audience.append(AudienceSubmission{comment->app_id, "message", *text, {}}, "観戦者");
        if (deps_.play_log) deps_.play_log->record_comment(comment->app_id, user_id, *text);
        return;
    }
    if (type == "pointer") {
        const auto pointer = parse_pointer_message(msg);
        if (!pointer) return error(session, "invalid pointer message");
        const auto* config = deps_.registry.config(pointer->app_id);
        if (!config || !session.app_ids.contains(pointer->app_id)) return;
        if (!deps_.control.acquire(pointer->app_id, session.id)) return;  // spectators' input is dropped
        if (config->input.allow_mouse && deps_.play_log) {
            deps_.play_log->record_input(session.id, session.user.id, pointer->app_id, PointerInput{pointer->x, pointer->y, pointer->button, pointer->phase});
        }
        deps_.input.send_pointer(*config, pointer->x, pointer->y, pointer->button, pointer->phase);
        return;
    }
    if (type != "key" && type != "click" && type != "button") return;
    const auto* config = deps_.registry.config(app_id);
    if (!config || !subscribed) return;
    if (!deps_.control.acquire(app_id, session.id)) return;  // spectators' input is dropped
    if (type == "key") {
        const auto key = string_field(msg, "key");
        if (!key) return;
        const auto down_it = msg.find("down");
        const bool down = down_it != msg.end() && truthy(*down_it);
        if (config->input.allow_keyboard && deps_.play_log) deps_.play_log->record_input(session.id, session.user.id, app_id, KeyInput{*key, down});
        deps_.input.send_key(*config, *key, down);
        return;
    }
    if (type == "click") {
        const auto x = msg.find("x"), y = msg.find("y");
        if (x == msg.end() || y == msg.end() || !x->is_number() || !y->is_number()) return;
        deps_.input.send_click(*config, x->get<double>(), y->get<double>(), string_field(msg, "button").value_or("left"));
        return;
    }
    // button
    const auto id = msg.find("id");
    const ButtonConfig* button = nullptr;
    for (const auto& b : config->input.buttons) if (id != msg.end() && id->is_string() && label_to_id(b.label) == id->get<std::string>()) { button = &b; break; }
    if (!button) return error(session, "unknown button: " + js_text(id != msg.end() ? &*id : nullptr));
    if (button->action == std::optional<std::string>("kill")) return error(session, "use POST /api/apps/:id/kill for kill button");
    if (button->key && !button->key->empty()) {
        if (deps_.play_log) deps_.play_log->record_input(session.id, session.user.id, app_id, ButtonInput{id->get<std::string>()});
        deps_.input.tap_key(*config, *button->key);
    }
}
}
