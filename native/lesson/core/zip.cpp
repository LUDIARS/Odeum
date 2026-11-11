#include "zip.hpp"
#include <array>
#include <stdexcept>

namespace odeum::lesson {
namespace {
constexpr std::uint16_t dos_date = ((2026 - 1980) << 9) | (1 << 5) | 1, dos_time = 0;
constexpr std::uint16_t utf8_names = 0x0800, version = 20;

std::array<std::uint32_t, 256> crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        auto c = n;
        for (int bit = 0; bit < 8; ++bit) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        table[n] = c;
    }
    return table;
}
void u16(std::string& out, std::uint32_t value) { out.push_back(static_cast<char>(value & 0xff)); out.push_back(static_cast<char>((value >> 8) & 0xff)); }
void u32(std::string& out, std::uint32_t value) { u16(out, value & 0xffff); u16(out, value >> 16); }
}
std::uint32_t crc32(std::string_view data) {
    static const auto table = crc_table();
    std::uint32_t crc = 0xffffffffu;
    for (const unsigned char byte : data) crc = table[(crc ^ byte) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}
std::string zip_store(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, directory;
    if (files.size() > 0xffff) throw std::length_error("Too many archive entries");
    for (const auto& [name, data] : files) {
        if (name.size() > 0xffff || data.size() > 0xffffffffu || out.size() > 0xffffffffu) throw std::length_error("Archive entry too large");
        const auto offset = static_cast<std::uint32_t>(out.size());
        const auto size = static_cast<std::uint32_t>(data.size());
        const auto crc = crc32(data);
        const auto header = [&](std::string& target) {
            u16(target, version); u16(target, utf8_names); u16(target, 0); u16(target, dos_time); u16(target, dos_date);
            u32(target, crc); u32(target, size); u32(target, size); u16(target, static_cast<std::uint32_t>(name.size())); u16(target, 0);
        };
        u32(out, 0x04034b50); header(out); out += name; out += data;
        u32(directory, 0x02014b50); u16(directory, version); header(directory);
        u16(directory, 0); u16(directory, 0); u16(directory, 0); u32(directory, 0); u32(directory, offset);
        directory += name;
    }
    const auto start = static_cast<std::uint32_t>(out.size());
    out += directory;
    u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, static_cast<std::uint32_t>(files.size())); u16(out, static_cast<std::uint32_t>(files.size()));
    u32(out, static_cast<std::uint32_t>(directory.size())); u32(out, start); u16(out, 0);
    return out;
}
}
