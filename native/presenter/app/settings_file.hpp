#pragma once
#include "../core/presenter_settings.hpp"
#include <filesystem>
#include <vector>

namespace odeum::presenter {
// Reads the settings file; a missing file yields the defaults. Throws std::invalid_argument
// (with the path) for unreadable JSON or invalid values so a broken file is never overwritten
// silently.
PresenterSettings load_settings(const std::filesystem::path&);
// Writes through a temporary file and a rename, creating the folder when needed.
void save_settings(const std::filesystem::path&, const PresenterSettings&);
// The configured font when set, else the first candidate that exists. Throws
// std::runtime_error when there is none, naming the --font option.
std::filesystem::path resolve_font(const PresenterSettings&, const std::vector<std::filesystem::path>& candidates);
}
