#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace odeum::relay {
struct HttpTarget { std::string path; std::map<std::string, std::string> query; };
// Splits a request target into path and percent-decoded query ('+' is a space). Duplicate or
// malformed parameters return nullopt rather than guessing which value was intended.
std::optional<HttpTarget> parse_target(std::string_view target);
}
