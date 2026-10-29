#include "web_access.hpp"
#include <algorithm>
#include <cctype>

namespace odeum::runner {
namespace {
std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
}

std::optional<std::string> authority_host(std::string_view authority) {
    if (authority.empty() || authority.find_first_of("/@?#\\ \t\r\n*") != std::string_view::npos) return std::nullopt;
    auto value = lower(authority);
    std::size_t port = std::string::npos;
    if (value.front() == '[') {
        const auto end = value.find(']');
        if (end == std::string::npos) return std::nullopt;
        if (end + 1 < value.size()) {
            if (value[end + 1] != ':') return std::nullopt;
            port = end + 1;
        }
    } else {
        port = value.find(':');
    }
    if (port != std::string::npos) {
        const auto digits = value.substr(port + 1);
        if (digits.empty() || digits.size() > 5 || !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); })) return std::nullopt;
        const auto number = std::stoul(digits);
        if (number == 0 || number > 65535) return std::nullopt;
        value.resize(port);
    }
    if (value.empty()) return std::nullopt;
    return value;
}

bool host_allowed(const WebAccessPolicy& policy, std::string_view host_header) {
    const auto host = authority_host(host_header);
    if (!host) return false;
    if (!policy.enforce_host) return true;
    if (*host == "localhost" || *host == "127.0.0.1" || *host == "[::1]") return true;
    if (!policy.bind_host.empty()) {
        const auto bind = lower(policy.bind_host);
        if (*host == bind || *host == "[" + bind + "]") return true;
    }
    for (const auto& entry : policy.extra_hosts) {
        const auto allowed = lower(entry);
        if (*host == allowed) return true;
        if (!allowed.empty() && allowed.front() == '.' &&
            (*host == allowed.substr(1) || (host->size() > allowed.size() && host->compare(host->size() - allowed.size(), allowed.size(), allowed) == 0))) return true;
    }
    return false;
}

bool origin_allowed(const WebAccessPolicy& policy, std::string_view origin, std::string_view host_header) {
    if (origin.empty()) return true;
    const auto o = lower(origin);
    const auto h = lower(host_header);
    if (!h.empty() && (o == "http://" + h || o == "https://" + h)) return true;
    for (const auto& allowed : policy.extra_origins) if (o == lower(allowed)) return true;
    return false;
}

bool is_state_changing(std::string_view method) {
    return method != "GET" && method != "HEAD" && method != "OPTIONS";
}
}
