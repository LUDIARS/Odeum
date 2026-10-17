#pragma once
#include "flv.hpp"
#include <filesystem>
#include <fstream>

namespace odeum::program {
// Reads only finalized Odeum recordings, one bounded FLV tag at a time.
class ArchiveReader {
public:
    explicit ArchiveReader(const std::filesystem::path& file);
    std::optional<FlvTag> next();
private:
    std::ifstream file_;
};
std::vector<std::filesystem::path> project_archives(const std::filesystem::path& root, const std::string& project);
}
