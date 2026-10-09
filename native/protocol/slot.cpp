#include <odeum/slot.hpp>

namespace odeum {
std::optional<std::size_t> input_index(std::string_view slot) {
    constexpr std::string_view prefix = "input";
    if (slot.size() != prefix.size() + 1 || !slot.starts_with(prefix)) return std::nullopt;
    const auto digit = slot.back();
    if (digit < '1' || digit > static_cast<char>('0' + max_input_slots)) return std::nullopt;
    return static_cast<std::size_t>(digit - '0');
}
std::string input_slot(std::size_t index) { return "input" + std::to_string(index); }
bool valid_slot(std::string_view slot) { return slot == program_slot || input_index(slot).has_value(); }
}
