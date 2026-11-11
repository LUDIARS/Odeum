#pragma once
#include <string_view>
#include <vector>

namespace odeum::lesson {
struct Asset { std::string_view content_type, body; };
// Pages (/lesson, /console, /web/*) and built-in course plots (/courses/*), embedded at build time.
// Returns nullptr for unknown paths.
const Asset* find_asset(std::string_view path);
std::vector<std::string_view> asset_paths(std::string_view prefix);
}
