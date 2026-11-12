#include <odeum/bounded_file.hpp>
#include <lorica/checked_size.hpp>
#include <array>
#include <fstream>

namespace odeum {
std::optional<std::string> read_file_within(const std::filesystem::path& path, std::size_t maximum) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string text;
    std::array<char, 16384> chunk{};
    // Read in fixed chunks and stop at maximum + 1 bytes: the size reported by the file system
    // can change between a check and the read.
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto got = static_cast<std::size_t>(input.gcount());
        const auto total = lorica::checked_add(text.size(), got);
        if (!total || *total > maximum) throw FileTooLarge("File exceeds " + std::to_string(maximum) + " bytes: " + path.string());
        text.append(chunk.data(), got);
    }
    if (input.bad()) throw std::runtime_error("Cannot read " + path.string());
    return text;
}
}
