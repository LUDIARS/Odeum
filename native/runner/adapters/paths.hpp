#pragma once
// UTF-8 strings <-> std::filesystem paths on every platform (Windows paths are UTF-16).
#include <filesystem>
#include <string>

namespace odeum::runner {
inline std::filesystem::path to_path(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
inline std::string from_path(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
}
