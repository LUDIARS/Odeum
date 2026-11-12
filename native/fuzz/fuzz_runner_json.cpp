// libFuzzer entry: one browser WebSocket message to odeum-runner (runner/core json.cpp and
// client_message.cpp), charged against the runner's Lorica budget.
#include "core/client_message.hpp"
#include "core/json.hpp"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace odeum::runner;
    const auto message = try_parse_json({reinterpret_cast<const char*>(data), size}, ws_message_limits);
    if (!message) return 0;
    (void)parse_pointer_message(*message);
    (void)parse_comment_message(*message);
    (void)parse_audience_submission(*message);
    return 0;
}
