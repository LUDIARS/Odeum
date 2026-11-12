#pragma once
// Limits on everything odeum-lesson reads from outside (job cpp-lorica-20261111). Each parser below
// charges one Lorica budget (odeum::parse_json_within) for the bytes, every value and key, and the
// nesting depth before anything is kept; spec/feature/ai-lesson.md lists the values.
#include <odeum/json_limits.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace odeum::lesson {
// One WebSocket text message from a student page or the operator console: the relay's limits
// (16 KiB, 1024 values, depth 8). The largest legal message is a multiple-choice answer (one index
// per choice) or a text of at most 400 characters, both far below.
inline constexpr JsonLimits message_limits = ws_message_limits;
// HTTP request headers and body. Tools post one small JSON object ({link, tool} or
// {assignment_id, step, done}); nothing else on this service takes a body.
inline constexpr std::uint32_t max_request_header_bytes = 16384;
inline constexpr std::uint64_t max_request_body_bytes = 4096;
// A tool's body: 4 KiB, 64 values (the largest legal body has 7), depth 4 (it is a flat object;
// depth 4 leaves room for a field a newer tool adds without letting a body nest arbitrarily).
inline constexpr JsonLimits tool_body_limits{max_request_body_bytes, 64, 4};
// A plot file written by a lesson author (ODEUM_LESSON_PLOT, the timeline / prep-sheet commands).
// 1 MiB was already the size limit; the built-in 90-minute course has 1,223 values at depth 7, so
// 16,384 values and depth 16 (the operator-file limits used elsewhere in Odeum) leave over 10x room.
inline constexpr JsonLimits plot_file_limits{1024 * 1024, 16384, 16};

// WebSocket text: valid UTF-8, within message_limits, a JSON object. Throws ProtocolError
// ("invalid_message") for every refusal, malformed JSON included.
Json parse_client_message(std::string_view wire);
// A tool's POST body: valid UTF-8 (else ProtocolError("invalid_message")), then within
// tool_body_limits and well-formed (else ProtocolError("invalid_request")).
Json parse_tool_body(std::string_view body);
// Plot text (a file or a built-in course): within plot_file_limits. Throws ProtocolError("invalid_plot").
Json parse_plot_text(std::string_view text);
}
