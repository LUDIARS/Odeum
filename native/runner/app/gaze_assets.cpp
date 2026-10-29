#include "web_assets.hpp"
#include <array>

namespace odeum::runner {
std::optional<GazeFile> gaze_asset(std::string_view path) {
    // The files custos/scripts/setup-gaze.mjs writes for @mediapipe/tasks-vision 1.0.1. Nothing else
    // under the directory is reachable, so traversal, symlink names and other files cannot be named.
    struct Entry { std::string_view name, type; };
    static constexpr std::array<Entry, 9> files{{
        {"vision_bundle.mjs", "text/javascript; charset=utf-8"},
        {"face_landmarker.task", "application/octet-stream"},
        {"NOTICE.txt", "text/plain; charset=utf-8"},
        {"wasm/vision_wasm_internal.js", "text/javascript; charset=utf-8"},
        {"wasm/vision_wasm_internal.wasm", "application/wasm"},
        {"wasm/vision_wasm_module_internal.js", "text/javascript; charset=utf-8"},
        {"wasm/vision_wasm_module_internal.wasm", "application/wasm"},
        {"wasm/vision_wasm_nosimd_internal.js", "text/javascript; charset=utf-8"},
        {"wasm/vision_wasm_nosimd_internal.wasm", "application/wasm"},
    }};
    static constexpr std::string_view prefix = "/gaze-assets/";
    if (path.substr(0, prefix.size()) != prefix) return std::nullopt;
    const auto name = path.substr(prefix.size());
    for (const auto& file : files) {
        if (file.name == name) return GazeFile{std::string(file.name), std::string(file.type)};
    }
    return std::nullopt;
}
}
