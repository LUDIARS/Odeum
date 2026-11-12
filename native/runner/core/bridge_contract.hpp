#pragma once
// Wire contract of the in-app HTTP bridge (ergo_custos / Unity custos-bridge) and the Unity editor
// routes: pure helpers ported from custos/src/capture/ergo-custos-client.ts, bridge-stream.ts,
// bridge-h264-frames.ts and custos/src/unity/bridge-*.ts.
#include "app_config.hpp"
#include "json.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace odeum::runner {
inline constexpr int default_unity_bridge_port = 17778;
inline constexpr std::size_t max_bridge_png_bytes = 32u * 1024 * 1024;

// GET /state -> { isPlaying, state }: state is kept only when it is a plain object.
struct BridgeState {
    bool is_playing = false;
    std::optional<Json> state;
};
BridgeState parse_bridge_state(const Json& body);

// GET /stream response headers -> frame metadata; nullopt when width/height/pixfmt are missing.
struct BridgeStreamMeta {
    int width = 0, height = 0;
    std::string pixfmt;
    int fps = 0;
};
std::optional<BridgeStreamMeta> parse_frame_headers(const std::map<std::string, std::string>& lower_headers, int fallback_fps);

bool looks_like_png(std::string_view bytes);

// Unity /api/unity/:appId target: only `kind: "unity"` bridges with a valid port.
struct BridgeTarget {
    std::string host;
    int port;
};
std::optional<BridgeTarget> resolve_bridge_target(const std::vector<AppConfig>& apps, const std::string& app_id);

// <unityProject>/custos/bridge-port contents -> port; nullopt when malformed.
std::optional<int> parse_bridge_port_file(std::string_view contents);

// compile --wait across a domain reload.
enum class CompileWaitPhase { waiting, done, unreachable };
struct CompileWaitState {
    bool ever_reachable = false;
    long long unreachable_ms = 0;
    long long unreachable_budget_ms = 90'000;
};
CompileWaitPhase next_compile_wait_phase(const CompileWaitState& state, bool reachable, bool is_compiling);

// application/x-www-form-urlencoded (URLSearchParams#toString) of ordered pairs.
std::string form_encode(const std::vector<std::pair<std::string, std::string>>& pairs);
}
