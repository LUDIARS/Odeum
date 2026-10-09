#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace odeum {
// Media slots of a room: senders publish to input1..inputN or to program (the default, which keeps the
// single-presenter layout). The relay caps N with ODEUM_RELAY_MAX_INPUTS; the wire format allows up to 8.
inline constexpr std::string_view program_slot = "program";
inline constexpr std::size_t max_input_slots = 8;
// 1-based index of "input<n>" (n = 1..8, no leading zero); nullopt for program and unknown names.
std::optional<std::size_t> input_index(std::string_view slot);
std::string input_slot(std::size_t index);
bool valid_slot(std::string_view slot);
}
