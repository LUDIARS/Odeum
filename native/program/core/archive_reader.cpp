#include "archive_reader.hpp"
#include <odeum/message.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace odeum::program {
namespace {
std::uint32_t be(std::span<const unsigned char> bytes) {
    std::uint32_t value = 0;
    for (auto b : bytes) value = (value << 8) | b;
    return value;
}
void read(std::ifstream& file, void* data, std::size_t size) {
    file.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    if (!file) throw std::runtime_error("archive_truncated");
}
bool finalized(const std::filesystem::path& file) {
    if (file.filename() != "program.flv" || std::filesystem::is_symlink(file) || !std::filesystem::is_regular_file(file)) return false;
    const auto manifest = file.parent_path() / "archive.json";
    if (!std::filesystem::is_regular_file(manifest) || std::filesystem::is_symlink(manifest) || std::filesystem::file_size(manifest) > 16384) return false;
    std::ifstream input(manifest, std::ios::binary);
    const auto json = Json::parse(input);
    return json.value("version", 0) == 1 && json.value("status", "") == "ready" && json.value("file", "") == "program.flv" &&
        json.value("bytes", std::uint64_t{}) == std::filesystem::file_size(file);
}
}
ArchiveReader::ArchiveReader(const std::filesystem::path& file) {
    if (!finalized(file)) throw std::invalid_argument("archive_not_finalized");
    file_.open(file, std::ios::binary);
    std::array<unsigned char, 13> header{};
    read(file_, header.data(), header.size());
    const std::array<unsigned char, 13> expected{'F','L','V',1,5,0,0,0,9,0,0,0,0};
    if (header != expected) throw std::invalid_argument("archive_invalid_header");
}
std::optional<FlvTag> ArchiveReader::next() {
    if (file_.peek() == std::char_traits<char>::eof()) {
        if (file_.bad()) throw std::runtime_error("archive_read_failed");
        return std::nullopt;
    }
    std::array<unsigned char, 11> header{};
    read(file_, header.data(), header.size());
    if ((header[0] != 8 && header[0] != 9 && header[0] != 18) || header[8] || header[9] || header[10])
        throw std::invalid_argument("archive_invalid_tag");
    const auto size = be(std::span(header).subspan(1,3));
    if (size > 8 * 1024 * 1024) throw std::invalid_argument("archive_tag_too_large");
    FlvTag tag{static_cast<FlvTagType>(header[0]), be(std::span(header).subspan(4,3)) | (std::uint32_t(header[7]) << 24), {}};
    tag.body.resize(size); read(file_, tag.body.data(), size);
    std::array<unsigned char, 4> previous{}; read(file_, previous.data(), previous.size());
    if (be(previous) != size + 11) throw std::invalid_argument("archive_invalid_size");
    return tag;
}
std::vector<std::filesystem::path> project_archives(const std::filesystem::path& root, const std::string& project) {
    if (project.empty() || project.size() > 80 || project.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos)
        throw std::invalid_argument("archive_invalid_project");
    std::vector<std::filesystem::path> files;
    const auto directory = root / project;
    if (!std::filesystem::exists(directory)) return files;
    if (std::filesystem::is_symlink(directory)) throw std::invalid_argument("archive_symlink_project");
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_symlink() || !entry.is_directory()) continue;
        const auto file = entry.path() / "program.flv";
        // Broken manifests are visible as a catalog error rather than silently skipped.
        if (finalized(file)) files.push_back(file);
    }
    std::sort(files.rbegin(), files.rend());
    return files;
}
}
