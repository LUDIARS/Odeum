#pragma once
#include <odeum/message.hpp>
#include <odeum/slot.hpp>
#include <set>

namespace odeum::relay {
// Throws slot_unavailable when an input slot exceeds the configured ODEUM_RELAY_MAX_INPUTS.
void require_slot(std::string_view slot, std::size_t max_inputs);
// The source viewers receive: program when live, otherwise the lowest-numbered live input.
std::optional<std::string> viewer_slot(const std::set<std::string>& live);
// presence / sessions view: input1..inputN and program, each true while a sender holds it.
Json slot_presence(const std::set<std::string>& connected, std::size_t max_inputs);
}
