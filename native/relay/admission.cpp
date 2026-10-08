#include "admission.hpp"
#include <openssl/rand.h>
#include <array>

namespace odeum::relay {
namespace {
std::string random_hex() {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) throw std::runtime_error("Random source unavailable");
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out; for (auto b : bytes) { out.push_back(digits[b >> 4]); out.push_back(digits[b & 15]); }
    return out;
}
std::string trimmed_name(std::string_view name) {
    auto begin = name.find_first_not_of(" \t"), end = name.find_last_not_of(" \t");
    if (begin == std::string_view::npos) throw ProtocolError("invalid_name", "Display name is required");
    std::string value(name.substr(begin, end - begin + 1));
    for (unsigned char c : value) if (c < 0x20 || c == 0x7f) throw ProtocolError("invalid_name", "Display name has control characters");
    std::size_t length = 0;
    try { length = utf8_length(value); } catch (const ProtocolError&) { throw ProtocolError("invalid_name", "Display name must be UTF-8"); }
    if (length > max_guest_name) throw ProtocolError("invalid_name", "Display name is too long");
    return value;
}
}
Ticket guest_ticket(const std::string& sid, std::string_view name) {
    return {"guest:" + random_hex(), trimmed_name(name), sid, "", Role::viewer, 0};
}
Ticket overlay_ticket(const std::string& sid) {
    return {"overlay:" + random_hex(), "Program overlay", sid, "", Role::overlay, 0};
}
}
