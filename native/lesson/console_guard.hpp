#pragma once
#include <string>
#include <string_view>

namespace odeum::lesson {
// The console runs the class (start, lock out, eject), so it is reachable only by someone
// operating this machine directly. Loopback narrows who can reach it; the token proves the
// caller can read a file only the operator's OS user can read.
struct ConsoleRequest {
    bool peer_loopback = false;
    bool forwarded = false;          // any Forwarded / X-Forwarded-* / X-Real-IP header present
    std::string host, origin, fetch_site;
};
enum class ConsoleDecision { allow, not_loopback, forwarded, bad_host, bad_origin, cross_site };
// Checks that apply to every console request, including the page and its scripts.
ConsoleDecision judge_console(const ConsoleRequest& request, unsigned port);
std::string_view console_decision_name(ConsoleDecision decision);
// Constant-time comparison; an empty expected token never matches.
bool same_token(std::string_view presented, std::string_view expected);
// Creates (or replaces) the token file readable by its owner only and returns the fresh token.
// Throws std::runtime_error when the file cannot be written.
std::string write_console_token(const std::string& path);
}
