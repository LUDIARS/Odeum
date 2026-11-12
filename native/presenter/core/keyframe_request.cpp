#include "keyframe_request.hpp"
#include <odeum/rtcp.hpp>

namespace odeum::presenter {
// The relay and the presenter share one reader (protocol/rtcp.cpp, Lorica-checked lengths).
bool is_keyframe_request(std::span<const std::byte> packet) { return odeum::is_keyframe_request(packet); }
}
