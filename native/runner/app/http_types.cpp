#include "http_types.hpp"

namespace odeum::runner {
namespace {
int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}

std::optional<std::string> percent_decode(std::string_view text, bool plus_is_space) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '%') {
            if (i + 2 >= text.size()) return std::nullopt;
            const int hi = hex_value(text[i + 1]), lo = hex_value(text[i + 2]);
            if (hi < 0 || lo < 0) return std::nullopt;
            out.push_back(static_cast<char>(hi * 16 + lo));
            i += 2;
        } else if (c == '+' && plus_is_space) {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return sanitize_utf8(out);
}

std::map<std::string, std::string> parse_query(std::string_view query) {
    std::map<std::string, std::string> out;
    std::size_t start = 0;
    while (start <= query.size()) {
        auto end = query.find('&', start);
        if (end == std::string_view::npos) end = query.size();
        const auto pair = query.substr(start, end - start);
        if (!pair.empty()) {
            const auto eq = pair.find('=');
            auto name = percent_decode(pair.substr(0, eq), true);
            auto value = eq == std::string_view::npos ? std::optional<std::string>("") : percent_decode(pair.substr(eq + 1), true);
            // URLSearchParams keeps malformed escapes literally.
            if (!name) name = std::string(pair.substr(0, eq));
            if (!value) value = std::string(pair.substr(eq + 1));
            out.emplace(*name, *value);
        }
        start = end + 1;
    }
    return out;
}

std::optional<std::vector<std::string>> path_segments(std::string_view path) {
    std::vector<std::string> out;
    std::size_t start = path.empty() || path.front() != '/' ? 0 : 1;
    while (start <= path.size()) {
        auto end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        auto segment = percent_decode(path.substr(start, end - start), false);
        if (!segment) return std::nullopt;
        out.push_back(*segment);
        start = end + 1;
    }
    return out;
}
}
