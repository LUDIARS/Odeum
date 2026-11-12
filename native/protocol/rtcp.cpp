#include <odeum/rtcp.hpp>
#include <lorica/byte_view.hpp>
#include <lorica/checked_size.hpp>

namespace odeum {
namespace {
std::optional<unsigned> octet(const lorica::byte_view& view, std::size_t index) {
    const auto value = view.at(index);
    if (!value) return std::nullopt;
    return std::to_integer<unsigned>(*value);
}
}
bool is_keyframe_request(std::span<const std::byte> packet) {
    const auto view = lorica::byte_view::make(packet.data(), packet.size(), max_rtp_packet_bytes);
    if (!view || view->size() < 4) return false;
    // RTCP: version 2 and a payload type in 192..223 (which also keeps RTP media out).
    const auto type = octet(*view, 1);
    if (!type || *type < 192 || *type > 223) return false;
    std::size_t offset = 0;
    while (lorica::contains_range(view->size(), offset, 4)) {
        const auto header = view->subview(offset, 4);
        const auto first = octet(*header, 0), kind = octet(*header, 1), high = octet(*header, 2), low = octet(*header, 3);
        if (!first || !kind || !high || !low || (*first >> 6) != 2) return false;
        // Length is in 32-bit words minus one.
        const auto words = lorica::checked_add((*high << 8) | *low, 1);
        const auto size = words ? lorica::checked_multiply(*words, 4) : std::nullopt;
        if (!size || !view->subview(offset, *size)) return false;
        const auto fmt = *first & 31;
        if (*kind == 206 && ((fmt == 1 && *size >= 12) || (fmt == 4 && *size >= 20))) return true;
        const auto next = lorica::checked_add(offset, *size);
        if (!next) return false;
        offset = *next;
    }
    return false;
}
}
