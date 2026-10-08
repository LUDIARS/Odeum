#include "http_query.hpp"

namespace odeum::relay {
namespace {
int hex(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
std::optional<std::string> decode(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') { out.push_back(' '); continue; }
        if (text[i] != '%') { out.push_back(text[i]); continue; }
        if (i + 2 >= text.size()) return std::nullopt;
        int high = hex(text[i + 1]), low = hex(text[i + 2]); if (high < 0 || low < 0) return std::nullopt;
        out.push_back(static_cast<char>(high * 16 + low)); i += 2;
    }
    return out;
}
}
std::optional<HttpTarget> parse_target(std::string_view target) {
    if (target.empty() || target.front() != '/' || target.find('#') != std::string_view::npos) return std::nullopt;
    HttpTarget result;
    auto mark = target.find('?');
    result.path = std::string(target.substr(0, mark));
    if (mark == std::string_view::npos) return result;
    auto rest = target.substr(mark + 1);
    while (!rest.empty()) {
        auto amp = rest.find('&'); auto pair = rest.substr(0, amp);
        rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
        auto eq = pair.find('='); if (eq == std::string_view::npos || eq == 0) return std::nullopt;
        auto key = decode(pair.substr(0, eq)), value = decode(pair.substr(eq + 1));
        if (!key || !value || !result.query.emplace(*key, *value).second) return std::nullopt;
    }
    return result;
}
}
