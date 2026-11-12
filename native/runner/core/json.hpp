#pragma once
// JSON for odeum-runner. Objects keep insertion order so wire output matches the former
// Node runner (JSON.stringify order). Every external JSON passes through parse_json, which
// charges a Lorica budget for the text, each value and the nesting depth before keeping it.
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace odeum::runner {
using Json = nlohmann::ordered_json;

struct JsonLimits {
    std::size_t bytes;
    std::size_t elements;
    std::size_t depth;
};

// HTTP request bodies (the former farm bodyLimit; SDP offers are far below it).
inline constexpr JsonLimits http_body_limits{256 * 1024, 16384, 32};
// One WebSocket text message from a browser.
inline constexpr JsonLimits ws_message_limits{64 * 1024, 1024, 8};
// Operator files (apps.json, farm configuration).
inline constexpr JsonLimits config_file_limits{4 * 1024 * 1024, 1u << 20, 32};
// Farm state file (written by this process; still external on read).
inline constexpr JsonLimits state_file_limits{8 * 1024 * 1024, 1u << 20, 16};
// Small service responses (Cernere verify, TURN credentials).
inline constexpr JsonLimits service_response_limits{64 * 1024, 4096, 16};
// In-app / Unity bridge JSON responses (hierarchy and logs can be large and nested).
inline constexpr JsonLimits bridge_response_limits{8 * 1024 * 1024, 1u << 20, 128};

class BudgetExceeded : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Throws BudgetExceeded when a limit is exceeded and Json::parse_error on malformed input.
Json parse_json(std::string_view text, const JsonLimits& limits);
// nullopt for malformed or over-budget input.
std::optional<Json> try_parse_json(std::string_view text, const JsonLimits& limits);

// Serialises without throwing on invalid UTF-8 (replaced by U+FFFD, as Node's Buffer decoding does).
std::string dump(const Json& value);
// A JavaScript number on the wire: integral values print without a fraction ("2", not "2.0").
Json js_number(double value);
// Replaces invalid UTF-8 sequences with U+FFFD (Node's Buffer#toString("utf8") behaviour).
std::string sanitize_utf8(std::string_view bytes);
}
