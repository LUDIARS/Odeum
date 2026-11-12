#include "settings.hpp"
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace odeum::runner {
EnvReader process_environment() {
    return [](const char* name) -> std::optional<std::string> {
        if (const char* value = std::getenv(name)) return std::string(value);
        return std::nullopt;
    };
}

namespace {
std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

std::uint16_t port(const EnvReader& env, const char* name, std::uint16_t fallback) {
    const auto value = env(name);
    if (!value) return fallback;
    unsigned n = 0;
    const auto text = trim(*value);
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (error != std::errc{} || end != text.data() + text.size() || n > 65535) throw SettingsError(std::string(name) + " must be a port number (0 disables)");
    return static_cast<std::uint16_t>(n);
}

std::vector<std::string> list(const std::optional<std::string>& value) {
    std::vector<std::string> out;
    if (!value) return out;
    std::istringstream input(*value);
    std::string item;
    while (std::getline(input, item, ',')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string join_path(const std::string& dir, const std::string& rest) {
    if (dir.empty()) return rest;
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + rest : dir + "/" + rest;
}

bool absolute(const std::string& path) {
    return (!path.empty() && (path.front() == '/' || path.front() == '\\')) || (path.size() > 1 && path[1] == ':');
}
}

RunnerSettings load_settings(const EnvReader& env, const std::string& cwd) {
    RunnerSettings s;
    s.backend_port = port(env, "CUSTOS_PORT", 7676);
    s.frontend_port = port(env, "CUSTOS_FRONTEND_PORT", 4649);
    s.host = env("CUSTOS_HOST").value_or("127.0.0.1");
    s.auth.open = env("CUSTOS_OPEN") == std::optional<std::string>("1");
    s.auth.auth_required = env("CUSTOS_AUTH_REQUIRED") == std::optional<std::string>("1");
    auto cernere = env("CERNERE_URL").value_or("");
    if (!cernere.empty() && cernere.back() == '/') cernere.pop_back();
    if (!cernere.empty() && cernere.rfind("http://", 0) != 0 && cernere.rfind("https://", 0) != 0) throw SettingsError("CERNERE_URL must be an http(s) URL");
    s.auth.cernere_url = cernere;

    const auto apps = env("CUSTOS_APPS_FILE");
    s.apps_file = apps && !apps->empty() ? *apps : join_path(cwd, "config/apps.json");
    const auto playlog = env("CUSTOS_PLAYLOG_DIR");
    s.playlog_dir = playlog && !playlog->empty() ? (absolute(*playlog) ? *playlog : join_path(cwd, *playlog)) : join_path(cwd, "logs/plays");
    if (auto salt = env("CUSTOS_PLAYLOG_SALT"); salt && !trim(*salt).empty()) s.playlog_salt = *salt;
    if (auto interval = env("CUSTOS_SCREENSHOT_INTERVAL_SEC"); interval && !interval->empty()) {
        char* end = nullptr;
        const double v = std::strtod(interval->c_str(), &end);
        // As before: an invalid or negative value is ignored (apps.json applies).
        if (end && *end == '\0' && std::isfinite(v) && v >= 0) s.screenshot_interval_sec = v;
    }
    s.ffmpeg = env("CUSTOS_FFMPEG").value_or("ffmpeg");
    s.adb = env("CUSTOS_ADB").value_or("adb");
    s.display = env("DISPLAY").value_or(":0.0");
    s.avfoundation_input = env("CUSTOS_AVFOUNDATION_INPUT").value_or("1:none");
    s.video_encoder = env("CUSTOS_VIDEO_ENCODER").value_or("auto");
    if (auto bitrate = env("CUSTOS_VIDEO_BITRATE"); bitrate && !bitrate->empty()) {
        char* end = nullptr;
        const double v = std::strtod(bitrate->c_str(), &end);
        if (!end || *end != '\0' || !std::isfinite(v) || v < 10'000 || v > 200'000'000) throw SettingsError("CUSTOS_VIDEO_BITRATE must be bits per second");
        s.video_bitrate = static_cast<long long>(v);
    }
    s.stun_urls = env("CUSTOS_STUN_URLS");
    s.turn_key_id = trim(env("CUSTOS_TURN_KEY_ID").value_or(""));
    s.turn_key_token = trim(env("CUSTOS_TURN_KEY_API_TOKEN").value_or(""));
    if (s.turn_key_id.empty() != s.turn_key_token.empty()) {
        throw SettingsError("CUSTOS_TURN_KEY_ID と CUSTOS_TURN_KEY_API_TOKEN は両方そろえて設定してください");
    }
    s.farm_config = env("CUSTOS_FARM_CONFIG");
    s.farm_state = env("CUSTOS_FARM_STATE_FILE");
    const auto gaze = env("ODEUM_RUNNER_GAZE_ASSETS_DIR");
    s.gaze_assets_dir = gaze && !gaze->empty() ? *gaze : join_path(cwd, "custos/public/gaze-assets");
    s.windir = env("WINDIR").value_or(env("SystemRoot").value_or(""));
    s.temp_dir = env("TEMP").value_or(env("TMP").value_or(env("TMPDIR").value_or("/tmp")));
    const auto loopback = env("ODEUM_RUNNER_PROCESS_LOOPBACK_SOURCE");
    s.process_loopback_source = loopback && !loopback->empty() ? *loopback : join_path(cwd, "custos/src/capture/process-loopback/ProcessLoopback.cs");
    s.access.bind_host = s.host;
    s.access.enforce_host = is_loopback_host(s.host);
    s.access.extra_hosts = list(env("LUDIARS_ALLOWED_HOSTS"));
    s.access.extra_origins = list(env("CUSTOS_ALLOWED_ORIGINS"));
    s.log_level = env("CUSTOS_LOG_LEVEL").value_or("info");
    return s;
}
}
