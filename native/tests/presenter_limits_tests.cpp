// Boundary tests for the presenter's inputs: the odeum:// launch link, the ticket's expiry and
// the settings file's text.
#include "check.hpp"
#include "core/launch_url.hpp"
#include "core/presenter_settings.hpp"
using namespace odeum;
using namespace odeum::presenter;

namespace {
std::string b64(const std::string& s) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    unsigned buffer = 0; int bits = 0;
    for (unsigned char c : s) {
        buffer = (buffer << 8) | c; bits += 8;
        while (bits >= 6) { bits -= 6; out.push_back(table[(buffer >> bits) & 63]); }
    }
    if (bits > 0) out.push_back(table[(buffer << (6 - bits)) & 63]);
    return out;
}
std::string ticket_with(const std::string& claims) {
    return b64(R"({"alg":"EdDSA","kid":"k"})") + "." + b64(claims) + "." + b64("signature-bytes");
}
std::string link(std::size_t extra) {
    std::string url = "odeum://present?relay=wss%3A%2F%2Frelay.example&ticket=" + ticket_with(R"({"exp":1})");
    for (std::size_t i = 0; i < extra; ++i) url += "&x" + std::to_string(i) + "=1";
    return url;
}
template<class F> void refuses(F action, const char* code) {
    try { action(); } catch (const LaunchUrlError& error) { check(error.code == code, code); return; }
    throw std::runtime_error(std::string("Expected rejection: ") + code);
}
template<class F> bool invalid(F action) {
    try { action(); } catch (const std::invalid_argument&) { return true; }
    return false;
}
std::string nested(std::size_t depth) { return std::string(depth, '[') + std::string(depth, ']'); }
}

int main() { return run([] {
    // relay + ticket + 14 unknown parameters = 16.
    check(parse_launch_url(link(max_launch_parameters - 2)).relay == "wss://relay.example", "Parameters at the limit");
    refuses([] { parse_launch_url(link(max_launch_parameters - 1)); }, "invalid_url");
    refuses([] { parse_launch_url("odeum://present?" + std::string(8000, '&')); }, "invalid_url");

    // ticket_expiry reads the unverified payload within the ticket part limits (depth 4).
    check(ticket_expiry(ticket_with(R"({"exp":5,"x":)" + nested(3) + "}")) == 5, "Claims at the depth limit");
    refuses([] { ticket_expiry(ticket_with(R"({"exp":5,"x":)" + nested(4) + "}")); }, "invalid_ticket");
    const auto padded = [](std::size_t size) {
        auto t = ticket_with(R"({"exp":5})");
        return t + std::string(size - t.size(), 'A');
    };
    check(ticket_expiry(padded(max_ticket_bytes)) == 5, "Ticket at the length limit");
    refuses([&] { ticket_expiry(padded(max_ticket_bytes + 1)); }, "invalid_ticket");
    refuses([] { ticket_expiry(ticket_with("{\"exp\":")); }, "invalid_ticket");

    // Settings text: within config_limits and the file size limit.
    check(settings_from_text(R"({"comments_visible":false})").comments_visible == false, "Settings text");
    std::string padded_settings = R"({"font":")" + std::string(max_settings_file_bytes - 11, 'f') + "\"}";
    check(padded_settings.size() == max_settings_file_bytes, "Padding arithmetic");
    check(settings_from_text(padded_settings).font.size() == max_settings_file_bytes - 11, "Settings at the size limit");
    check(invalid([&] { settings_from_text(padded_settings + " "); }), "Settings one byte over");
    settings_from_text(R"({"x":)" + nested(15) + "}");
    check(invalid([] { settings_from_text(R"({"x":)" + nested(16) + "}"); }), "Settings nested past the limit");
    check(invalid([] { settings_from_text("{"); }), "Broken settings");
}); }
