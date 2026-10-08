#pragma once
#include <string_view>

namespace odeum::relay {
struct WebAsset { std::string_view content_type, body; };
// Guest join and program overlay pages, embedded at build time from native/relay/web.
// Returns nullptr for unknown paths; the relay never reads the filesystem to serve pages.
const WebAsset* find_web_asset(std::string_view path);
}
