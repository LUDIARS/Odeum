#pragma once
// Bounded JSON parsing for everything Odeum reads from outside (WebSocket messages, tickets,
// configuration and settings files, archive manifests). Each call owns one Lorica budget that is
// charged for the text, every value and key, and the nesting depth before the value is kept.
#include <odeum/message.hpp>
#include <cstddef>
#include <string_view>

namespace odeum {
struct JsonLimits {
    std::size_t bytes;
    std::size_t elements;  // values and object keys
    std::size_t depth;     // open arrays/objects
};
// One relay WebSocket text message. The largest legal one is welcome (16 ICE servers of 16 URLs).
inline constexpr JsonLimits ws_message_limits{max_message_bytes, 1024, 8};
// One decoded JWS part (header or claims) of a ticket.
inline constexpr JsonLimits ticket_part_limits{8192, 256, 4};
// Operator-written files and environment values (keys, ICE servers, app settings).
inline constexpr JsonLimits config_limits{1024 * 1024, 16384, 16};
// Largest operator file read whole (public keys, settings).
inline constexpr std::size_t max_config_file_bytes = 1024 * 1024;

// Throws ProtocolError("invalid_message") when a limit is exceeded and Json::parse_error when the
// text is malformed.
Json parse_json_within(std::string_view text, const JsonLimits& limits);
}
