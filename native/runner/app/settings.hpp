#pragma once
// Environment of odeum-runner. The names are those of the former Node runner (CUSTOS_*), so an
// existing site configuration keeps working; new names are ODEUM_RUNNER_* / LUDIARS_*.
#include "../core/auth_policy.hpp"
#include "../core/web_access.hpp"
#include <functional>
#include <optional>
#include <string>

namespace odeum::runner {
using EnvReader = std::function<std::optional<std::string>(const char*)>;
EnvReader process_environment();

class SettingsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct RunnerSettings {
    std::uint16_t backend_port = 7676;   // CUSTOS_PORT (0 disables)
    std::uint16_t frontend_port = 4649;  // CUSTOS_FRONTEND_PORT (0 disables)
    std::string host = "127.0.0.1";      // CUSTOS_HOST
    AuthSettings auth;                   // CUSTOS_OPEN / CERNERE_URL / CUSTOS_AUTH_REQUIRED
    std::string apps_file;               // CUSTOS_APPS_FILE (default <cwd>/config/apps.json)
    std::string playlog_dir;             // CUSTOS_PLAYLOG_DIR (default <cwd>/logs/plays)
    std::optional<std::string> playlog_salt;           // CUSTOS_PLAYLOG_SALT
    std::optional<double> screenshot_interval_sec;     // CUSTOS_SCREENSHOT_INTERVAL_SEC
    std::string ffmpeg = "ffmpeg";                     // CUSTOS_FFMPEG
    std::string adb = "adb";                           // CUSTOS_ADB
    std::string display = ":0.0";                      // DISPLAY (x11grab)
    std::string avfoundation_input = "1:none";         // CUSTOS_AVFOUNDATION_INPUT
    std::string video_encoder = "auto";                // CUSTOS_VIDEO_ENCODER
    long long video_bitrate = 4'000'000;               // CUSTOS_VIDEO_BITRATE
    std::optional<std::string> stun_urls;              // CUSTOS_STUN_URLS (unset = Cloudflare STUN)
    std::string turn_key_id, turn_key_token;           // CUSTOS_TURN_KEY_ID / CUSTOS_TURN_KEY_API_TOKEN
    std::optional<std::string> farm_config, farm_state;  // CUSTOS_FARM_CONFIG / CUSTOS_FARM_STATE_FILE
    std::string gaze_assets_dir;                       // ODEUM_RUNNER_GAZE_ASSETS_DIR (default custos/public/gaze-assets)
    std::string windir;                                // WINDIR / SystemRoot (csc.exe for process audio)
    std::string temp_dir;                              // TEMP / TMP / TMPDIR (ProcessLoopback.exe cache)
    std::string process_loopback_source;               // ODEUM_RUNNER_PROCESS_LOOPBACK_SOURCE (default custos/src/capture/process-loopback/ProcessLoopback.cs)
    WebAccessPolicy access;                            // LUDIARS_ALLOWED_HOSTS / CUSTOS_ALLOWED_ORIGINS
    std::string log_level = "info";                    // CUSTOS_LOG_LEVEL
};

// Throws SettingsError for malformed values (ports, numbers, URLs, a half-configured TURN key).
RunnerSettings load_settings(const EnvReader& env, const std::string& cwd);
}
