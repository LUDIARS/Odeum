#pragma once
// Reads a whole operator file (settings, keys, manifests) without letting its size decide how much
// memory is taken: anything over the caller's maximum is refused before it is kept.
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace odeum {
struct FileTooLarge : std::runtime_error { using std::runtime_error::runtime_error; };
// nullopt when the file cannot be opened; throws FileTooLarge over `maximum` bytes and
// std::runtime_error on a read error.
std::optional<std::string> read_file_within(const std::filesystem::path& path, std::size_t maximum);
}
