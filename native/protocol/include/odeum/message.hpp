#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace odeum {
using Json = nlohmann::json;
inline constexpr std::size_t max_message_bytes = 16 * 1024;
// overlay is relay-internal (program overlay key); tickets never carry it.
enum class Role { presenter, viewer, service, overlay };
enum class MessageType { welcome, sdp, candidate, good, stamp, comment, poll_open,
    poll_close, poll_closed, poll_answer, tally, reaction_burst, presence, error, telop, submission, reaction_ready };
struct ProtocolError : std::runtime_error {
    std::string code;
    ProtocolError(std::string c, std::string message) : runtime_error(message), code(std::move(c)) {}
};
struct Message { MessageType type; Json body; };
std::size_t utf8_length(std::string_view text);
void require_text(const Json& object, std::string_view key, std::size_t maximum, bool empty = false);
Message parse_message(std::string_view wire);
std::string serialize_message(const Message& message);
std::string role_name(Role role);
Role parse_role(std::string_view role);
Json error_message(std::string_view code, std::string_view message);
}
