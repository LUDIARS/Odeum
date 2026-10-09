#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace odeum::presenter {
// What GLab hands the presenter: the relay's WebSocket base and a presenter ticket.
struct LaunchRequest {
    std::string relay;  // wss://host[:port][/prefix] without a trailing slash
    std::string ticket; // JWS compact
    bool operator==(const LaunchRequest&) const = default;
};

struct LaunchUrlError : std::runtime_error {
    // invalid_url | unsupported_scheme | unsupported_action | relay_missing | insecure_relay |
    // invalid_relay | ticket_missing | invalid_ticket
    std::string code;
    LaunchUrlError(std::string c, const std::string& message) : runtime_error(message), code(std::move(c)) {}
};

// Reads `odeum://present?relay=<wss URL>&ticket=<JWS>` (values percent-encoded, any order,
// surrounding whitespace ignored so a pasted link works). Rejects every other scheme or action,
// a relay that is not wss://, and a missing or malformed ticket. Throws LaunchUrlError.
LaunchRequest parse_launch_url(std::string_view text);
// The same for another action: odeum-program takes `odeum://produce?relay=...&ticket=...`.
// `expected_action` is lower case.
LaunchRequest parse_launch_url(std::string_view text, std::string_view expected_action);

// The relay's WebSocket endpoint for this ticket: <relay>/v1/ws?ticket=<ticket>.
std::string relay_socket_url(const LaunchRequest&);

// Seconds since the Unix epoch at which the ticket stops being accepted, read from the
// unverified JWS payload (the relay verifies the signature). Throws LaunchUrlError when the
// payload carries no integer exp.
std::int64_t ticket_expiry(std::string_view ticket);
}
