#pragma once
#include <odeum/ticket.hpp>

namespace odeum::relay {
inline constexpr std::size_t max_guest_name = 32;
// Builds the relay-side identity for a guest who entered a valid join code. The subject is a fresh
// random value per connection; the display name is trimmed and must be 1-32 scalars without controls.
Ticket guest_ticket(const std::string& sid, std::string_view name);
// Identity for a program overlay (OBS browser source). It never sends and is not counted as a viewer.
Ticket overlay_ticket(const std::string& sid);
}
