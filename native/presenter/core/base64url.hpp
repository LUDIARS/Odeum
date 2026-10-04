#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace odeum::presenter {
// Unpadded base64url (RFC 4648 section 5) as used by JWS compact segments. Any other character,
// padding or an impossible length yields nullopt.
std::optional<std::string> decode_base64url(std::string_view text);
}
