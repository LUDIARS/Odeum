#include "unityctl.hpp"
#include <cmath>

namespace odeum::runner {
namespace {
const std::vector<std::string> commands{"status", "health", "compile", "errors", "log", "play", "stop", "scene", "hierarchy", "capture"};

std::string require_value(const std::vector<std::string>& rest, std::size_t index, const std::string& flag) {
    if (index >= rest.size() || rest[index].rfind("--", 0) == 0) throw UnityctlArgError(flag + " requires a value");
    return rest[index];
}

// Number(raw) must be an integer >= minimum.
long long require_int(const std::vector<std::string>& rest, std::size_t index, const std::string& flag, long long minimum) {
    const auto raw = require_value(rest, index, flag);
    // Number(raw): surrounding whitespace is ignored and an empty string is 0.
    const auto begin = raw.find_first_not_of(" \t\r\n");
    const auto text = begin == std::string::npos ? std::string("0") : raw.substr(begin, raw.find_last_not_of(" \t\r\n") - begin + 1);
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    const bool numeric = end && *end == '\0' && text.find_first_of("xXnN") == std::string::npos;
    if (!numeric || !std::isfinite(value) || std::floor(value) != value || value < static_cast<double>(minimum) || value > 9e15) {
        throw UnityctlArgError(flag + " requires an integer >= " + std::to_string(minimum) + " (got " + raw + ")");
    }
    return static_cast<long long>(value);
}

std::string text(const Json& object, const char* key) {
    auto it = object.find(key);
    if (it == object.end() || it->is_null()) return {};
    if (it->is_string()) return it->get<std::string>();
    return it->dump();
}

bool flag(const Json& object, const char* key) {
    auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

double number(const Json& object, const char* key) {
    auto it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<double>() : 0;
}

std::string number_text(const Json& object, const char* key) {
    auto it = object.find(key);
    if (it == object.end()) return "undefined";
    if (it->is_number()) return js_number(it->get<double>()).dump();
    return text(object, key);
}
}

std::pair<std::string, std::optional<int>> split_host_port(const std::string& value) {
    if (!value.empty() && value.find_first_not_of("0123456789") == std::string::npos) {
        return {"127.0.0.1", value.size() > 9 ? -1 : std::stoi(value)};
    }
    const auto index = value.rfind(':');
    if (index == std::string::npos) return {value, std::nullopt};
    const auto host = value.substr(0, index);
    const auto port_text = value.substr(index + 1);
    const bool digits = !port_text.empty() && port_text.size() <= 5 && port_text.find_first_not_of("0123456789") == std::string::npos;
    const int port = digits ? std::stoi(port_text) : 0;
    if (!digits || port <= 0 || port > 65535) throw UnityctlArgError("invalid port in --bridge " + value);
    return {host.empty() ? "127.0.0.1" : host, port};
}

UnityctlArgs parse_unityctl_args(const std::vector<std::string>& argv) {
    if (argv.empty()) throw UnityctlArgError("command is required");
    const auto& command = argv.front();
    bool known = false;
    for (const auto& c : commands) known |= c == command;
    if (!known) throw UnityctlArgError("unknown command: " + command);
    UnityctlArgs args;
    args.command = command;
    const std::vector<std::string> rest(argv.begin() + 1, argv.end());
    for (std::size_t i = 0; i < rest.size(); ++i) {
        const auto& f = rest[i];
        if (f == "--json") args.json = true;
        else if (f == "--wait") args.wait = true;
        else if (f == "--bridge") { auto [host, port] = split_host_port(require_value(rest, ++i, f)); args.host = host; args.port = port; }
        else if (f == "--token") args.token = require_value(rest, ++i, f);
        else if (f == "--since") args.since = require_int(rest, ++i, f, 0);
        else if (f == "--level") args.level = require_value(rest, ++i, f);
        else if (f == "--limit") args.limit = require_int(rest, ++i, f, 1);
        else if (f == "--depth") args.depth = require_int(rest, ++i, f, 0);
        else if (f == "--root") args.root = require_value(rest, ++i, f);
        else if (f == "--caption") args.caption = require_value(rest, ++i, f);
        else if (f == "--source") args.source = require_value(rest, ++i, f);
        else throw UnityctlArgError("unknown option: " + f);
    }
    return args;
}

std::string unityctl_usage() {
    return "usage: unityctl <command> [options]\n\ncommands:\n  health | status | scene | hierarchy | log | play | stop\n"
           "  compile [--wait]     再コンパイル。--wait でリロードを跨いで完了まで待つ\n"
           "  errors               コンパイルエラーのみ表示 (エラーありで exit 1)\n"
           "  capture --caption T  キャプチャを AI セッションへ送る\n\noptions:\n"
           "  --bridge <host:port|port>   接続先 (既定 127.0.0.1 + custos/bridge-port または 17778)\n"
           "  --token <token>             X-Auth-Token\n  --json                      機械可読出力\n"
           "  --since/--level/--limit     log 用\n  --depth/--root              hierarchy 用\n  --caption/--source          capture 用";
}

int compile_exit_code(const Json& status) { return number(status, "errorCount") > 0 ? exit_compile_error : exit_ok; }

std::string render_status(const Json& s) {
    const bool playing = flag(s, "isPlaying"), paused = flag(s, "isPaused");
    const std::string mode = playing ? (paused ? "playing (paused)" : "playing") : "edit";
    std::string busy;
    if (flag(s, "isCompiling")) busy = "compiling";
    if (flag(s, "isReloading")) busy += busy.empty() ? "reloading" : ", reloading";
    const auto scene = text(s, "activeScene");
    std::string out = "Unity     " + text(s, "unityVersion") + "\nproject   " + text(s, "projectPath") + "\nmode      " + mode +
        (busy.empty() ? "" : " [" + busy + "]") + "\nscene     " + (scene.empty() ? "(none)" : scene);
    if (auto dirty = s.find("dirtyScenes"); dirty != s.end() && dirty->is_array() && !dirty->empty()) {
        out += "\ndirty     ";
        bool first = true;
        for (const auto& d : *dirty) { out += (first ? "" : ", ") + (d.is_string() ? d.get<std::string>() : d.dump()); first = false; }
    }
    return out;
}

std::string format_compile_message(const Json& m) {
    const auto file = text(m, "file");
    const auto location = file.empty() ? std::string("(unknown location)") : file + ":" + number_text(m, "line") + ":" + number_text(m, "column");
    const auto assembly = text(m, "assembly");
    return location + (assembly.empty() ? "" : " [" + assembly + "]") + " " + text(m, "message");
}

std::string render_compile_status(const Json& s) {
    if (flag(s, "isCompiling")) return "compiling...";
    const double errors = number(s, "errorCount"), warnings = number(s, "warningCount");
    if (errors == 0 && warnings == 0) return "no compile errors or warnings";
    std::string out = js_number(errors).dump() + " error(s), " + js_number(warnings).dump() + " warning(s)";
    if (auto list = s.find("errors"); list != s.end() && list->is_array()) for (const auto& e : *list) out += "\n  ERROR " + format_compile_message(e);
    if (auto list = s.find("warnings"); list != s.end() && list->is_array()) for (const auto& w : *list) out += "\n  WARN  " + format_compile_message(w);
    return out;
}

std::string render_log(const Json& page) {
    auto entries = page.find("entries");
    if (entries == page.end() || !entries->is_array() || entries->empty()) return "(no new entries; nextSeq=" + number_text(page, "nextSeq") + ")";
    std::string out;
    bool first = true;
    for (const auto& e : *entries) {
        out += (first ? "" : "\n") + number_text(e, "seq") + " " + text(e, "ts") + " [" + text(e, "level") + "] " + text(e, "message");
        first = false;
    }
    return out;
}

std::string render_json(const Json& value) { return value.dump(2, ' ', false, Json::error_handler_t::replace); }
}
