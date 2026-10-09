#include "slots.hpp"

namespace odeum::relay {
void require_slot(std::string_view slot, std::size_t max_inputs) {
    if (slot == program_slot) return;
    const auto index = input_index(slot);
    if (!index || *index > max_inputs) throw ProtocolError("slot_unavailable", "Slot is not available on this relay");
}
std::optional<std::string> viewer_slot(const std::set<std::string>& live) {
    if (live.contains(std::string(program_slot))) return std::string(program_slot);
    std::optional<std::string> best;
    for (const auto& slot : live) {
        const auto index = input_index(slot);
        if (index && (!best || *index < *input_index(*best))) best = slot;
    }
    return best;
}
Json slot_presence(const std::set<std::string>& connected, std::size_t max_inputs) {
    Json slots = Json::object();
    for (std::size_t i = 1; i <= max_inputs; ++i) slots[input_slot(i)] = connected.contains(input_slot(i));
    slots[std::string(program_slot)] = connected.contains(std::string(program_slot));
    return slots;
}
}
