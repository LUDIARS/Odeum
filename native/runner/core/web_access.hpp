#pragma once
// Browser-facing request checks added by the port (the Node runner had none):
//  - Host allowlist while bound to loopback, so a DNS-rebinding page cannot reach the anonymous
//    runner through another name;
//  - same-origin requirement for state-changing requests and WebSocket upgrades, so a page on
//    another origin cannot start builds or inject input through the user's browser (CSRF / CSWSH).
// Non-browser clients (no Origin header) are unaffected.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace odeum::runner {
struct WebAccessPolicy {
    bool enforce_host = true;               // true while the bind address is loopback
    std::vector<std::string> extra_hosts;    // LUDIARS_ALLOWED_HOSTS (".example.org" allows subdomains)
    std::vector<std::string> extra_origins;  // CUSTOS_ALLOWED_ORIGINS (exact scheme://host[:port])
    std::string bind_host;
};

// Lower-case host of an HTTP authority without the port; nullopt when malformed.
std::optional<std::string> authority_host(std::string_view authority);
bool host_allowed(const WebAccessPolicy& policy, std::string_view host_header);
bool origin_allowed(const WebAccessPolicy& policy, std::string_view origin, std::string_view host_header);
bool is_state_changing(std::string_view method);
}
