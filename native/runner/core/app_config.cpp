#include "app_config.hpp"
#include <cmath>
#include <set>

namespace odeum::runner {
namespace {
[[noreturn]] void fail(const std::string& path, const std::string& message) {
    throw ConfigError(path + ": " + message);
}

std::string at(const std::string& path, const std::string& key) {
    return path.empty() ? key : path + "." + key;
}

// zod `.optional()` accepts a missing key but rejects an explicit null.
const Json* field(const Json& object, const char* key, const std::string& path) {
    auto it = object.find(key);
    if (it == object.end()) return nullptr;
    if (it->is_null()) fail(at(path, key), "expected a value, got null");
    return &*it;
}

void require_object(const Json& value, const std::string& path) {
    if (!value.is_object()) fail(path, "expected an object");
}

std::string string_value(const Json& value, const std::string& path, std::size_t min_length) {
    if (!value.is_string()) fail(path, "expected a string");
    auto text = value.get<std::string>();
    if (text.size() < min_length) fail(path, "must not be empty");
    return text;
}

std::optional<std::string> optional_string(const Json& object, const char* key, const std::string& path, std::size_t min_length = 0) {
    const auto* value = field(object, key, path);
    if (!value) return std::nullopt;
    return string_value(*value, at(path, key), min_length);
}

std::string required_string(const Json& object, const char* key, const std::string& path, std::size_t min_length = 0) {
    const auto* value = field(object, key, path);
    if (!value) fail(at(path, key), "required");
    return string_value(*value, at(path, key), min_length);
}

std::string string_or(const Json& object, const char* key, const std::string& path, const char* fallback) {
    auto value = optional_string(object, key, path);
    return value ? *value : std::string(fallback);
}

bool bool_or(const Json& object, const char* key, const std::string& path, bool fallback) {
    const auto* value = field(object, key, path);
    if (!value) return fallback;
    if (!value->is_boolean()) fail(at(path, key), "expected a boolean");
    return value->get<bool>();
}

double number(const Json& value, const std::string& path) {
    if (!value.is_number()) fail(path, "expected a number");
    const double v = value.get<double>();
    if (!std::isfinite(v)) fail(path, "expected a finite number");
    return v;
}

// JavaScript numbers: an integer is any finite number without a fraction (1e2 included).
long long integer(const Json& value, const std::string& path, double min, double max) {
    const double v = number(value, path);
    if (std::floor(v) != v) fail(path, "expected an integer");
    if (v < min || v > max) fail(path, "out of range");
    return static_cast<long long>(v);
}

long long integer_or(const Json& object, const char* key, const std::string& path, double min, double max, long long fallback) {
    const auto* value = field(object, key, path);
    return value ? integer(*value, at(path, key), min, max) : fallback;
}

double number_or(const Json& object, const char* key, const std::string& path, double min, double max, double fallback) {
    const auto* value = field(object, key, path);
    if (!value) return fallback;
    const double v = number(*value, at(path, key));
    if (v < min || v > max) fail(at(path, key), "out of range");
    return v;
}

std::string one_of(const std::string& value, std::initializer_list<const char*> allowed, const std::string& path) {
    for (const auto* option : allowed) if (value == option) return value;
    fail(path, "unsupported value");
}

std::vector<std::string> string_array(const Json& object, const char* key, const std::string& path) {
    std::vector<std::string> result;
    const auto* value = field(object, key, path);
    if (!value) return result;
    if (!value->is_array()) fail(at(path, key), "expected an array");
    std::size_t index = 0;
    for (const auto& item : *value) result.push_back(string_value(item, at(path, key) + "[" + std::to_string(index++) + "]", 0));
    return result;
}

CmdConfig parse_cmd(const Json& value, const std::string& path) {
    require_object(value, path);
    CmdConfig cmd;
    const auto working_dir = optional_string(value, "workingDir", path, 1);
    const auto cwd = optional_string(value, "cwd", path, 1);
    cmd.cmd = required_string(value, "cmd", path, 1);
    cmd.args = string_array(value, "args", path);
    if (const auto* env = field(value, "env", path)) {
        if (!env->is_object()) fail(at(path, "env"), "expected an object");
        for (const auto& [name, entry] : env->items()) cmd.env.emplace_back(name, string_value(entry, at(at(path, "env"), name), 0));
    }
    if (const auto* timeout = field(value, "timeoutSec", path)) cmd.timeout_sec = integer(*timeout, at(path, "timeoutSec"), 0, 1e15);
    if (!working_dir && !cwd) fail(at(path, "workingDir"), "workingDir (or legacy `cwd`) is required");
    cmd.working_dir = working_dir ? *working_dir : *cwd;
    return cmd;
}

CaptureConfig parse_capture(const Json& value, const std::string& path) {
    require_object(value, path);
    CaptureConfig capture;
    capture.type = one_of(required_string(value, "type", path), {"window", "fullscreen", "android"}, at(path, "type"));
    capture.window_title = optional_string(value, "windowTitle", path);
    capture.android_serial = optional_string(value, "androidSerial", path);
    capture.fps = static_cast<int>(integer_or(value, "fps", path, 1, 60, 30));
    capture.preset = string_or(value, "preset", path, "veryfast");
    capture.interval_sec = number_or(value, "intervalSec", path, 0, 60, 1);
    return capture;
}

ButtonConfig parse_button(const Json& value, const std::string& path) {
    require_object(value, path);
    ButtonConfig button;
    button.label = required_string(value, "label", path, 1);
    button.key = optional_string(value, "key", path);
    if (auto action = optional_string(value, "action", path)) button.action = one_of(*action, {"kill", "screenshot", "noop"}, at(path, "action"));
    // zod refine: Boolean(key) !== Boolean(action); an empty key counts as absent.
    const bool has_key = button.key && !button.key->empty();
    const bool has_action = button.action.has_value();
    if (has_key == has_action) fail(path, "button must specify exactly one of `key` or `action`");
    return button;
}

std::vector<ButtonConfig> button_array(const Json& object, const char* key, const std::string& path) {
    std::vector<ButtonConfig> result;
    const auto* value = field(object, key, path);
    if (!value) return result;
    if (!value->is_array()) fail(at(path, key), "expected an array");
    std::size_t index = 0;
    for (const auto& item : *value) result.push_back(parse_button(item, at(path, key) + "[" + std::to_string(index++) + "]"));
    return result;
}

DirectionKeys parse_directions(const Json& value, const std::string& path, bool stick) {
    require_object(value, path);
    DirectionKeys keys{required_string(value, "up", path, 1), required_string(value, "down", path, 1),
        required_string(value, "left", path, 1), required_string(value, "right", path, 1), std::nullopt};
    if (stick) keys.press = optional_string(value, "press", path, 1);
    return keys;
}

PadConfig parse_pad(const Json& value, const std::string& path) {
    require_object(value, path);
    PadConfig pad;
    if (const auto* v = field(value, "leftStick", path)) pad.left_stick = parse_directions(*v, at(path, "leftStick"), true);
    if (const auto* v = field(value, "rightStick", path)) pad.right_stick = parse_directions(*v, at(path, "rightStick"), true);
    if (const auto* v = field(value, "dpad", path)) pad.dpad = parse_directions(*v, at(path, "dpad"), false);
    if (const auto* v = field(value, "face", path)) {
        const auto p = at(path, "face");
        require_object(*v, p);
        pad.face = FaceKeys{optional_string(*v, "a", p, 1), optional_string(*v, "b", p, 1), optional_string(*v, "x", p, 1), optional_string(*v, "y", p, 1)};
    }
    if (const auto* v = field(value, "shoulders", path)) {
        const auto p = at(path, "shoulders");
        require_object(*v, p);
        pad.shoulders = ShoulderKeys{optional_string(*v, "lb", p, 1), optional_string(*v, "rb", p, 1), optional_string(*v, "lt", p, 1), optional_string(*v, "rt", p, 1)};
    }
    pad.menu = button_array(value, "menu", path);
    return pad;
}

InputConfig parse_input(const Json& value, const std::string& path) {
    require_object(value, path);
    InputConfig input;
    input.buttons = button_array(value, "buttons", path);
    if (const auto* pad = field(value, "pad", path)) input.pad = parse_pad(*pad, at(path, "pad"));
    input.allow_keyboard = bool_or(value, "allowKeyboard", path, true);
    input.allow_mouse = bool_or(value, "allowMouse", path, true);
    return input;
}

LogsConfig parse_logs(const Json& value, const std::string& path) {
    require_object(value, path);
    LogsConfig logs;
    logs.stdout_lines = bool_or(value, "stdout", path, true);
    logs.stderr_lines = bool_or(value, "stderr", path, true);
    logs.files = string_array(value, "files", path);
    return logs;
}

BridgeVideoConfig parse_video(const Json& value, const std::string& path) {
    require_object(value, path);
    BridgeVideoConfig video;
    if (auto codec = optional_string(value, "codec", path)) video.codec = one_of(*codec, {"auto", "h264", "rawvideo"}, at(path, "codec"));
    video.width = static_cast<int>(integer_or(value, "width", path, 64, 3840, 1280));
    video.height = static_cast<int>(integer_or(value, "height", path, 64, 2160, 720));
    video.bitrate = integer_or(value, "bitrate", path, 100'000, 50'000'000, 3'000'000);
    video.min_bitrate = integer_or(value, "minBitrate", path, 100'000, 50'000'000, 300'000);
    video.max_bitrate = integer_or(value, "maxBitrate", path, 100'000, 50'000'000, 6'000'000);
    if (!(video.min_bitrate <= video.bitrate && video.bitrate <= video.max_bitrate)) fail(path, "video: minBitrate <= bitrate <= maxBitrate");
    return video;
}

InAppBridgeConfig parse_bridge(const Json& value, const std::string& path) {
    require_object(value, path);
    InAppBridgeConfig bridge;
    if (auto kind = optional_string(value, "kind", path)) bridge.kind = one_of(*kind, {"ergo", "unity"}, at(path, "kind"));
    bridge.host = string_or(value, "host", path, "127.0.0.1");
    const auto* port = field(value, "port", path);
    bridge.fps = static_cast<int>(integer_or(value, "fps", path, 1, 120, 24));
    if (const auto* video = field(value, "video", path)) bridge.video = parse_video(*video, at(path, "video"));
    bridge.port = port ? static_cast<int>(integer(*port, at(path, "port"), 1, 65535)) : (bridge.kind == "unity" ? 17778 : 5198);
    return bridge;
}

InAppBridgeConfig parse_ergo(const Json& value, const std::string& path) {
    require_object(value, path);
    InAppBridgeConfig bridge;  // kind ergo, fps 24 and the default video, as the former normalisation
    bridge.host = string_or(value, "host", path, "127.0.0.1");
    bridge.port = static_cast<int>(integer_or(value, "port", path, 1, 65535, 5198));
    return bridge;
}

PlayLogConfig parse_play_log(const Json& value, const std::string& path) {
    require_object(value, path);
    PlayLogConfig log;
    log.enabled = bool_or(value, "enabled", path, true);
    log.state_poll_ms = static_cast<int>(integer_or(value, "statePollMs", path, 100, 10'000, 500));
    log.idle_stop_sec = static_cast<int>(integer_or(value, "idleStopSec", path, 10, 86'400, 300));
    return log;
}

AppConfig parse_app(const Json& value, const std::string& path) {
    require_object(value, path);
    AppConfig app;
    app.id = required_string(value, "id", path);
    if (!is_valid_app_id(app.id)) fail(at(path, "id"), "id は英数字 + ハイフン");
    app.name = required_string(value, "name", path, 1);
    app.description = optional_string(value, "description", path);
    if (auto target = optional_string(value, "target", path)) app.target = one_of(*target, {"desktop", "android"}, at(path, "target"));
    if (const auto* v = field(value, "build", path)) app.build = parse_cmd(*v, at(path, "build"));
    const auto* run = field(value, "run", path);
    if (!run) fail(at(path, "run"), "required");
    app.run = parse_cmd(*run, at(path, "run"));
    if (const auto* v = field(value, "test", path)) app.test = parse_cmd(*v, at(path, "test"));
    if (const auto* v = field(value, "capture", path)) app.capture = parse_capture(*v, at(path, "capture"));
    std::optional<InAppBridgeConfig> ergo;
    if (const auto* v = field(value, "ergoCustos", path)) ergo = parse_ergo(*v, at(path, "ergoCustos"));
    if (const auto* v = field(value, "inAppBridge", path)) app.in_app_bridge = parse_bridge(*v, at(path, "inAppBridge"));
    if (!app.in_app_bridge) app.in_app_bridge = ergo;  // inAppBridge wins when both are given
    if (const auto* v = field(value, "input", path)) app.input = parse_input(*v, at(path, "input"));
    if (const auto* v = field(value, "logs", path)) app.logs = parse_logs(*v, at(path, "logs"));
    if (const auto* v = field(value, "playLog", path)) app.play_log = parse_play_log(*v, at(path, "playLog"));
    if (const auto* v = field(value, "audio", path)) {
        const auto p = at(path, "audio");
        require_object(*v, p);
        if (required_string(*v, "source", p) != "process-loopback") fail(at(p, "source"), "expected \"process-loopback\"");
        app.audio = true;
    }
    return app;
}

Json cmd_json(const CmdConfig& cmd) {
    Json env = Json::object();
    for (const auto& [name, value] : cmd.env) env[name] = value;
    Json out{{"workingDir", cmd.working_dir}, {"cwd", cmd.working_dir}, {"cmd", cmd.cmd}, {"args", cmd.args}, {"env", env}};
    if (cmd.timeout_sec) out["timeoutSec"] = *cmd.timeout_sec;
    return out;
}

Json directions_json(const DirectionKeys& keys) {
    Json out{{"up", keys.up}, {"down", keys.down}, {"left", keys.left}, {"right", keys.right}};
    if (keys.press) out["press"] = *keys.press;
    return out;
}

void put(Json& out, const char* key, const std::optional<std::string>& value) {
    if (value) out[key] = *value;
}
}

bool is_valid_app_id(std::string_view id) {
    auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
    if (id.empty() || !alnum(id.front())) return false;
    for (char c : id) if (!alnum(c) && c != '-') return false;
    return true;
}

AppsConfig parse_apps_config(const Json& root) {
    require_object(root, "(root)");
    if (const auto* version = field(root, "version", "")) {
        if (!version->is_number() || version->get<double>() != 1) fail("version", "expected 1");
    }
    const auto* apps = field(root, "apps", "");
    if (!apps) fail("apps", "required");
    if (!apps->is_array()) fail("apps", "expected an array");
    AppsConfig config;
    std::set<std::string> ids;
    std::size_t index = 0;
    for (const auto& item : *apps) config.apps.push_back(parse_app(item, "apps[" + std::to_string(index++) + "]"));
    for (const auto& app : config.apps) {
        if (!ids.insert(app.id).second) throw ConfigError("duplicate app id: " + app.id);
    }
    return config;
}

Json to_json(const ButtonConfig& button) {
    Json out{{"label", button.label}};
    put(out, "key", button.key);
    put(out, "action", button.action);
    return out;
}

Json to_json(const InputConfig& input) {
    Json buttons = Json::array();
    for (const auto& b : input.buttons) buttons.push_back(to_json(b));
    Json out{{"buttons", buttons}};
    if (input.pad) {
        Json pad = Json::object();
        if (input.pad->left_stick) pad["leftStick"] = directions_json(*input.pad->left_stick);
        if (input.pad->right_stick) pad["rightStick"] = directions_json(*input.pad->right_stick);
        if (input.pad->dpad) pad["dpad"] = directions_json(*input.pad->dpad);
        if (input.pad->face) {
            Json face = Json::object();
            put(face, "a", input.pad->face->a); put(face, "b", input.pad->face->b);
            put(face, "x", input.pad->face->x); put(face, "y", input.pad->face->y);
            pad["face"] = face;
        }
        if (input.pad->shoulders) {
            Json shoulders = Json::object();
            put(shoulders, "lb", input.pad->shoulders->lb); put(shoulders, "rb", input.pad->shoulders->rb);
            put(shoulders, "lt", input.pad->shoulders->lt); put(shoulders, "rt", input.pad->shoulders->rt);
            pad["shoulders"] = shoulders;
        }
        Json menu = Json::array();
        for (const auto& b : input.pad->menu) menu.push_back(to_json(b));
        pad["menu"] = menu;
        out["pad"] = pad;
    }
    out["allowKeyboard"] = input.allow_keyboard;
    out["allowMouse"] = input.allow_mouse;
    return out;
}

Json to_json(const AppConfig& app) {
    Json out{{"id", app.id}, {"name", app.name}};
    put(out, "description", app.description);
    out["target"] = app.target;
    if (app.build) out["build"] = cmd_json(*app.build);
    out["run"] = cmd_json(app.run);
    if (app.test) out["test"] = cmd_json(*app.test);
    if (app.capture) {
        Json capture{{"type", app.capture->type}};
        put(capture, "windowTitle", app.capture->window_title);
        put(capture, "androidSerial", app.capture->android_serial);
        capture["fps"] = app.capture->fps;
        capture["preset"] = app.capture->preset;
        capture["intervalSec"] = js_number(app.capture->interval_sec);
        out["capture"] = capture;
    }
    out["input"] = to_json(app.input);
    out["logs"] = Json{{"stdout", app.logs.stdout_lines}, {"stderr", app.logs.stderr_lines}, {"files", app.logs.files}};
    if (app.play_log) out["playLog"] = Json{{"enabled", app.play_log->enabled}, {"statePollMs", app.play_log->state_poll_ms}, {"idleStopSec", app.play_log->idle_stop_sec}};
    if (app.audio) out["audio"] = Json{{"source", "process-loopback"}};
    if (app.in_app_bridge) {
        const auto& b = *app.in_app_bridge;
        out["inAppBridge"] = Json{{"kind", b.kind}, {"host", b.host}, {"port", b.port}, {"fps", b.fps},
            {"video", Json{{"codec", b.video.codec}, {"width", b.video.width}, {"height", b.video.height}, {"bitrate", b.video.bitrate},
                {"minBitrate", b.video.min_bitrate}, {"maxBitrate", b.video.max_bitrate}}}};
    }
    return out;
}
}
