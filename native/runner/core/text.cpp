#include "text.hpp"

namespace odeum::runner {
std::vector<char32_t> code_points(std::string_view s) {
    std::vector<char32_t> out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        std::size_t n = 1;
        if (c >= 0xF0) { cp = c & 0x07; n = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
        for (std::size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::string encode_utf8(const std::vector<char32_t>& points) {
    std::string out;
    for (char32_t cp : points) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::size_t utf16_length(std::string_view s) {
    std::size_t units = 0;
    for (char32_t cp : code_points(s)) units += cp > 0xFFFF ? 2 : 1;
    return units;
}

std::size_t code_point_length(std::string_view s) { return code_points(s).size(); }

bool is_js_whitespace(char32_t c) {
    switch (c) {
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0: case 0x1680:
        case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
            return true;
        default:
            return c >= 0x2000 && c <= 0x200A;
    }
}

std::string js_trim(std::string_view s) {
    auto points = code_points(s);
    std::size_t begin = 0, end = points.size();
    while (begin < end && is_js_whitespace(points[begin])) ++begin;
    while (end > begin && is_js_whitespace(points[end - 1])) --end;
    return encode_utf8(std::vector<char32_t>(points.begin() + static_cast<std::ptrdiff_t>(begin), points.begin() + static_cast<std::ptrdiff_t>(end)));
}

std::string utf16_prefix(std::string_view s, std::size_t units) {
    std::vector<char32_t> kept;
    std::size_t used = 0;
    for (char32_t cp : code_points(s)) {
        const std::size_t width = cp > 0xFFFF ? 2 : 1;
        if (used + width > units) break;
        used += width;
        kept.push_back(cp);
    }
    return encode_utf8(kept);
}
}
