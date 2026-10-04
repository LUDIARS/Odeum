#include "keyframe_request.hpp"

namespace odeum::presenter {
namespace {
unsigned at(std::span<const std::byte> p, std::size_t i) { return std::to_integer<unsigned>(p[i]); }
}
bool is_keyframe_request(std::span<const std::byte> packet) {
    // RTCP: version 2 and a payload type in 192..223 (which also keeps RTP media out).
    if (packet.size() < 4 || (at(packet, 0) >> 6) != 2 || at(packet, 1) < 192 || at(packet, 1) > 223) return false;
    for (std::size_t offset = 0; offset + 4 <= packet.size();) {
        if ((at(packet, offset) >> 6) != 2) return false;
        const std::size_t size = ((at(packet, offset + 2) << 8) | at(packet, offset + 3)) * 4 + 4;
        if (offset + size > packet.size()) return false;
        const auto fmt = at(packet, offset) & 31;
        if (at(packet, offset + 1) == 206 && ((fmt == 1 && size >= 12) || (fmt == 4 && size >= 20))) return true;
        offset += size;
    }
    return false;
}
}
