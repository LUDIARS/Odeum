#include "base64url.hpp"

namespace odeum::presenter {
namespace {
int value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}
}
std::optional<std::string> decode_base64url(std::string_view text) {
    if (text.size() % 4 == 1) return std::nullopt;
    std::string result;
    result.reserve(text.size() * 3 / 4);
    unsigned buffer = 0;
    int bits = 0;
    for (char c : text) {
        const int v = value(c);
        if (v < 0) return std::nullopt;
        buffer = (buffer << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<char>((buffer >> bits) & 0xff));
        }
    }
    // Leftover bits must be zero, otherwise the text is not a canonical encoding.
    if (bits > 0 && (buffer & ((1u << bits) - 1)) != 0) return std::nullopt;
    return result;
}
}
