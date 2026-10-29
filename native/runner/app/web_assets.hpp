#pragma once
// The browser UI (custos/public), embedded at build time. The server never reads the file system
// for these pages. The optional on-device gaze model (prepared by custos/scripts/setup-gaze.mjs)
// is the only file-system content, and only under fixed names.
#include <optional>
#include <string>
#include <string_view>

namespace odeum::runner {
struct WebAsset {
    std::string_view content_type, body;
};
// nullptr for unknown paths.
const WebAsset* find_web_asset(std::string_view path);

// "/gaze-assets/<allowlisted name>" -> (relative file name, content type); nullopt otherwise.
struct GazeFile {
    std::string relative;
    std::string content_type;
};
std::optional<GazeFile> gaze_asset(std::string_view path);
}
