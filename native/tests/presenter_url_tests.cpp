#include "check.hpp"
#include "core/launch_url.hpp"
#include "core/presenter_options.hpp"
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
std::string ticket(std::int64_t exp = 1700000300) {
    return b64(R"({"alg":"EdDSA","kid":"k"})") + "." + b64("{\"exp\":" + std::to_string(exp) + ",\"role\":\"presenter\"}") + "." + b64("signature-bytes");
}
template<class F> void refuses(F action, const char* code) {
    try { action(); } catch (const LaunchUrlError& error) { check(error.code == code, code); return; }
    throw std::runtime_error(std::string("Expected rejection: ") + code);
}
}

int main() { return run([] {
    const auto t = ticket();
    // GLab encodes both values with encodeURIComponent.
    auto request = parse_launch_url("odeum://present?relay=wss%3A%2F%2Frelay.example.jp%2Fodeum%2F&ticket=" + t);
    check(request.relay == "wss://relay.example.jp/odeum" && request.ticket == t, "Encoded link");
    check(relay_socket_url(request) == "wss://relay.example.jp/odeum/v1/ws?ticket=" + t, "Socket URL");
    check(parse_launch_url("  ODEUM://present/?ticket=" + t + "&relay=wss://r.example:8443\n").relay == "wss://r.example:8443", "Pasted link, any order");
    check(parse_launch_url("odeum://present?relay=WSS://r.example&ticket=" + t + "#frag").relay == "wss://r.example", "Scheme case and fragment");
    check(ticket_expiry(t) == 1700000300, "Ticket expiry");

    refuses([&] { parse_launch_url("https://glab.example/present?relay=wss://r&ticket=" + t); }, "unsupported_scheme");
    refuses([&] { parse_launch_url("odeum-x://present?relay=wss://r&ticket=" + t); }, "unsupported_scheme");
    refuses([&] { parse_launch_url("odeum://view?relay=wss://r&ticket=" + t); }, "unsupported_action");
    // odeum-program's producer link: same rules, another action, and neither accepts the other's.
    check(parse_launch_url("odeum://produce?relay=wss://r.example&ticket=" + t, "produce").relay == "wss://r.example", "Producer link");
    refuses([&] { parse_launch_url("odeum://produce?relay=wss://r&ticket=" + t); }, "unsupported_action");
    refuses([&] { parse_launch_url("odeum://present?relay=wss://r&ticket=" + t, "produce"); }, "unsupported_action");
    refuses([&] { parse_launch_url("odeum://produce?relay=ws://r&ticket=" + t, "produce"); }, "insecure_relay");
    refuses([&] { parse_launch_url("odeum://present?relay=ws://r.example&ticket=" + t); }, "insecure_relay");
    refuses([&] { parse_launch_url("odeum://present?relay=https://r.example&ticket=" + t); }, "insecure_relay");
    refuses([&] { parse_launch_url("odeum://present?ticket=" + t); }, "relay_missing");
    refuses([&] { parse_launch_url("odeum://present?relay=wss://&ticket=" + t); }, "invalid_relay");
    refuses([&] { parse_launch_url("odeum://present?relay=wss://user@r.example&ticket=" + t); }, "invalid_relay");
    refuses([&] { parse_launch_url("odeum://present?relay=wss://r.example/?x=1&ticket=" + t); }, "invalid_relay");
    refuses([] { parse_launch_url("odeum://present?relay=wss://r.example"); }, "ticket_missing");
    refuses([] { parse_launch_url("odeum://present?relay=wss://r.example&ticket="); }, "ticket_missing");
    refuses([] { parse_launch_url("odeum://present?relay=wss://r.example&ticket=abc.def"); }, "invalid_ticket");
    refuses([] { parse_launch_url("odeum://present?relay=wss://r.example&ticket=a.b.c.d"); }, "invalid_ticket");
    refuses([] { parse_launch_url("odeum://present?relay=wss://r.example&ticket=a+b.c.d"); }, "invalid_ticket");
    refuses([&] { parse_launch_url("odeum://present?relay=wss://a&relay=wss://b&ticket=" + t); }, "invalid_url");
    refuses([&] { parse_launch_url("odeum://present?relay=wss%3&ticket=" + t); }, "invalid_url");
    refuses([] { ticket_expiry(b64("{}") + "." + b64("{\"role\":\"presenter\"}") + "." + b64("s")); }, "invalid_ticket");

    // Command line: the link is kept for the panel, options override the stored settings.
    PresenterSettings stored;
    stored.stream.fps = 24;
    auto options = parse_options({"-psn_0_1234", "odeum://present?x", "--max-bitrate-kbps", "4000", "--audio", "--overlay-corner", "top-left"}, stored);
    check(options.launch_url == "odeum://present?x" && options.settings.stream.fps == 24, "Stored settings kept");
    check(options.settings.stream.max_bitrate_kbps == 4000 && options.settings.stream.audio && options.settings.overlay.corner == "top-left", "Overrides");
    check(parse_options({"--register-url-scheme"}, {}).action == StartupAction::register_url_scheme, "Registration action");
    bool refused = false;
    try { parse_options({"--fps", "120"}, {}); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, "Out-of-range fps");
    refused = false;
    try { parse_options({"--overlay-corner", "middle"}, {}); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, "Unknown corner");
    refused = false;
    try { parse_options({"--width"}, {}); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, "Missing value");
}); }
