#include "console_guard.hpp"
#include "school.hpp"
#include <array>
#include <filesystem>
#include <fstream>

namespace odeum::lesson {
ConsoleDecision judge_console(const ConsoleRequest& request, unsigned port) {
    if (!request.peer_loopback) return ConsoleDecision::not_loopback;
    // A tunnel or reverse proxy on this machine also connects from loopback; its requests carry these headers.
    if (request.forwarded) return ConsoleDecision::forwarded;
    const auto suffix = ":" + std::to_string(port);
    const std::array<std::string, 3> hosts{"127.0.0.1" + suffix, "localhost" + suffix, "[::1]" + suffix};
    // A loopback Host name stops DNS rebinding: a hostile page cannot make its own name resolve here and pass.
    bool host_ok = false;
    for (const auto& host : hosts) host_ok = host_ok || request.host == host;
    if (!host_ok) return ConsoleDecision::bad_host;
    if (!request.origin.empty() && request.origin != "http://" + request.host) return ConsoleDecision::bad_origin;
    if (request.fetch_site == "cross-site" || request.fetch_site == "same-site") return ConsoleDecision::cross_site;
    return ConsoleDecision::allow;
}
std::string_view console_decision_name(ConsoleDecision decision) {
    constexpr std::array names{"allow", "not_loopback", "forwarded", "bad_host", "bad_origin", "cross_site"};
    return names.at(static_cast<std::size_t>(decision));
}
bool same_token(std::string_view presented, std::string_view expected) {
    if (expected.empty() || presented.size() != expected.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) difference |= static_cast<unsigned char>(presented[i] ^ expected[i]);
    return difference == 0;
}
std::string write_console_token(const std::string& path) {
    namespace fs = std::filesystem;
    const auto token = random_token() + random_token();
    std::error_code error;
    fs::remove(path, error);
    // Create empty, restrict, then write: the token never exists in a file other users can read.
    { std::ofstream create(path, std::ios::binary); if (!create) throw std::runtime_error("Cannot create console token file"); }
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, error);
    if (error) throw std::runtime_error("Cannot restrict console token file");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << token << '\n';
    if (!file.flush()) throw std::runtime_error("Cannot write console token file");
    return token;
}
}
