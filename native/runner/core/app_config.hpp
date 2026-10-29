#pragma once
// apps.json: the operator's pre-registered target apps. Port of custos/src/config/apps-config.ts
// (zod schema): same fields, defaults, ranges, the legacy `cwd` / `ergoCustos` aliases and the
// rule that unknown keys are dropped. Pure: the caller reads the file.
#include "json.hpp"
#include <optional>
#include <string>
#include <vector>

namespace odeum::runner {
class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CmdConfig {
    std::string working_dir;  // workingDir, else the legacy cwd; both name the same directory
    std::string cmd;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> env;  // insertion order of the file
    std::optional<long long> timeout_sec;
};

struct CaptureConfig {
    std::string type;  // window | fullscreen | android
    std::optional<std::string> window_title, android_serial;
    int fps = 30;
    std::string preset = "veryfast";
    double interval_sec = 1;
};

struct ButtonConfig {
    std::string label;
    std::optional<std::string> key, action;  // action: kill | screenshot | noop
};

struct DirectionKeys {
    std::string up, down, left, right;
    std::optional<std::string> press;  // sticks only
};

struct FaceKeys {
    std::optional<std::string> a, b, x, y;
};

struct ShoulderKeys {
    std::optional<std::string> lb, rb, lt, rt;
};

struct PadConfig {
    std::optional<DirectionKeys> left_stick, right_stick, dpad;
    std::optional<FaceKeys> face;
    std::optional<ShoulderKeys> shoulders;
    std::vector<ButtonConfig> menu;
};

struct InputConfig {
    std::vector<ButtonConfig> buttons;
    std::optional<PadConfig> pad;
    bool allow_keyboard = true;
    bool allow_mouse = true;
};

struct LogsConfig {
    bool stdout_lines = true, stderr_lines = true;
    std::vector<std::string> files;
};

struct BridgeVideoConfig {
    std::string codec = "auto";  // auto | h264 | rawvideo
    int width = 1280, height = 720;
    long long bitrate = 3'000'000, min_bitrate = 300'000, max_bitrate = 6'000'000;
};

struct InAppBridgeConfig {
    std::string kind = "ergo";  // ergo | unity
    std::string host = "127.0.0.1";
    int port = 5198;
    int fps = 24;
    BridgeVideoConfig video;
};

struct PlayLogConfig {
    bool enabled = true;
    int state_poll_ms = 500;
    int idle_stop_sec = 300;
};

struct AppConfig {
    std::string id, name;
    std::optional<std::string> description;
    std::string target = "desktop";  // desktop | android
    std::optional<CmdConfig> build;
    CmdConfig run;
    std::optional<CmdConfig> test;
    std::optional<CaptureConfig> capture;
    std::optional<InAppBridgeConfig> in_app_bridge;  // ergoCustos is normalised into this
    InputConfig input;
    LogsConfig logs;
    std::optional<PlayLogConfig> play_log;
    bool audio = false;  // audio: { source: "process-loopback" }
};

struct AppsConfig {
    std::vector<AppConfig> apps;
};

// Validates and normalises an already parsed apps.json root. Throws ConfigError naming the path.
AppsConfig parse_apps_config(const Json& root);
// The normalised configuration in the former zod output shape (tests and golden comparison).
Json to_json(const AppConfig& app);
Json to_json(const InputConfig& input);
Json to_json(const ButtonConfig& button);
bool is_valid_app_id(std::string_view id);
}
