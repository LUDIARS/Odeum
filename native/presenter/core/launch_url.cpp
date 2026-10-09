#include "launch_url.hpp"
#include "base64url.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <optional>

namespace odeum::presenter {
namespace {
constexpr std::string_view prefix = "odeum://";
constexpr std::size_t max_url_bytes = 8192;

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::string percent_decode(std::string_view text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') { result.push_back(text[i]); continue; }
        if (i + 2 >= text.size() || hex(text[i + 1]) < 0 || hex(text[i + 2]) < 0)
            throw LaunchUrlError("invalid_url", "Broken percent-encoding in the launch link");
        result.push_back(static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2])));
        i += 2;
    }
    return result;
}
bool equals_lower(std::string_view text, std::string_view lower) {
    return text.size() == lower.size() && std::equal(text.begin(), text.end(), lower.begin(),
        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
}
std::string validate_relay(std::string relay) {
    if (relay.empty()) throw LaunchUrlError("relay_missing", "The launch link has no relay");
    const auto scheme = relay.find("://");
    if (scheme == std::string::npos) throw LaunchUrlError("invalid_relay", "The relay is not a URL");
    if (!equals_lower(std::string_view(relay).substr(0, scheme), "wss"))
        throw LaunchUrlError("insecure_relay", "The relay must be a wss:// URL");
    relay.replace(0, scheme, "wss");
    const auto rest = std::string_view(relay).substr(scheme + 3);
    const auto host_end = rest.find('/');
    const auto authority = rest.substr(0, host_end);
    if (authority.empty() || authority.front() == ':' || authority.find('@') != std::string_view::npos)
        throw LaunchUrlError("invalid_relay", "The relay host is missing or carries credentials");
    for (char c : rest) {
        if (c == '?' || c == '#' || static_cast<unsigned char>(c) <= ' ' || c == '\\')
            throw LaunchUrlError("invalid_relay", "The relay URL must not carry a query, fragment or spaces");
    }
    while (relay.size() > scheme + 3 + authority.size() && relay.back() == '/') relay.pop_back();
    return relay;
}
void validate_ticket(const std::string& ticket) {
    if (ticket.empty()) throw LaunchUrlError("ticket_missing", "The launch link has no ticket");
    const auto first = ticket.find('.');
    const auto second = first == std::string::npos ? std::string::npos : ticket.find('.', first + 1);
    if (second == std::string::npos || ticket.find('.', second + 1) != std::string::npos)
        throw LaunchUrlError("invalid_ticket", "The ticket is not a JWS compact token");
    const std::string_view view(ticket);
    for (auto part : {view.substr(0, first), view.substr(first + 1, second - first - 1), view.substr(second + 1)}) {
        if (part.empty() || !decode_base64url(part)) throw LaunchUrlError("invalid_ticket", "The ticket is not base64url");
    }
}
}

LaunchRequest parse_launch_url(std::string_view text) { return parse_launch_url(text, "present"); }

LaunchRequest parse_launch_url(std::string_view text, std::string_view expected_action) {
    text = trim(text);
    if (text.size() > max_url_bytes) throw LaunchUrlError("invalid_url", "The launch link is too long");
    const auto colon = text.find(':');
    if (colon == std::string_view::npos) throw LaunchUrlError("invalid_url", "Not a URL");
    if (!equals_lower(text.substr(0, colon), "odeum") || text.substr(colon, 3) != "://")
        throw LaunchUrlError("unsupported_scheme", "Only odeum:// links are accepted");
    auto rest = text.substr(prefix.size());
    if (const auto hash = rest.find('#'); hash != std::string_view::npos) rest = rest.substr(0, hash);
    const auto question = rest.find('?');
    auto action = rest.substr(0, question);
    while (!action.empty() && action.back() == '/') action.remove_suffix(1);
    if (!equals_lower(action, expected_action))
        throw LaunchUrlError("unsupported_action", "Only odeum://" + std::string(expected_action) + " is accepted");
    std::optional<std::string> relay, ticket;
    auto query = question == std::string_view::npos ? std::string_view{} : rest.substr(question + 1);
    while (!query.empty()) {
        const auto amp = query.find('&');
        const auto pair = query.substr(0, amp);
        query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
        const auto eq = pair.find('=');
        const auto key = pair.substr(0, eq);
        const auto value = eq == std::string_view::npos ? std::string{} : percent_decode(pair.substr(eq + 1));
        // A repeated key is ambiguous, so it is refused rather than resolved silently.
        if (key == "relay") { if (relay) throw LaunchUrlError("invalid_url", "relay appears twice"); relay = value; }
        else if (key == "ticket") { if (ticket) throw LaunchUrlError("invalid_url", "ticket appears twice"); ticket = value; }
    }
    LaunchRequest request{validate_relay(relay.value_or("")), ticket.value_or("")};
    validate_ticket(request.ticket);
    return request;
}

std::string relay_socket_url(const LaunchRequest& request) {
    return request.relay + "/v1/ws?ticket=" + request.ticket;
}

std::int64_t ticket_expiry(std::string_view ticket) {
    const auto first = ticket.find('.');
    const auto second = first == std::string_view::npos ? std::string_view::npos : ticket.find('.', first + 1);
    if (second == std::string_view::npos) throw LaunchUrlError("invalid_ticket", "The ticket is not a JWS compact token");
    const auto payload = decode_base64url(ticket.substr(first + 1, second - first - 1));
    if (!payload) throw LaunchUrlError("invalid_ticket", "The ticket payload is not base64url");
    const auto claims = nlohmann::json::parse(*payload, nullptr, false);
    if (!claims.is_object() || !claims.contains("exp") || !claims["exp"].is_number_integer())
        throw LaunchUrlError("invalid_ticket", "The ticket carries no expiry");
    return claims["exp"].get<std::int64_t>();
}
}
