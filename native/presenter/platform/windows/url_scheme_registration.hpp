#pragma once
#include <filesystem>

namespace odeum::presenter::windows {
// HKCU\Software\Classes\odeum: the per-user URL protocol, so a GLab link starts this executable
// with the link as its argument. No administrator rights are needed and nothing outside the
// current user's hive is touched. Throws std::runtime_error when the registry refuses.
void register_url_scheme(const std::filesystem::path& executable);
void unregister_url_scheme();
}
