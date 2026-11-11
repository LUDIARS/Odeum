#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace odeum::lesson {
// Writes a ZIP archive with stored (uncompressed) entries, which is all an OOXML package needs.
// Output depends only on the input: entry timestamps are fixed so the same sheet is byte-identical.
std::string zip_store(const std::vector<std::pair<std::string, std::string>>& files);
std::uint32_t crc32(std::string_view data);
}
