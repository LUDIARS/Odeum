#include "rtcp.hpp"
#include <lorica/checked_size.hpp>

namespace odeum::runner {
namespace {
unsigned byte(std::string_view p, std::size_t i) { return static_cast<unsigned char>(p[i]); }
}

bool is_rtcp(std::string_view p) {
    return p.size() >= 4 && (byte(p, 0) >> 6) == 2 && byte(p, 1) >= 192 && byte(p, 1) <= 223;
}

RtcpFeedback parse_rtcp_feedback(std::string_view p) {
    RtcpFeedback out;
    if (!is_rtcp(p)) return out;
    std::size_t offset = 0;
    while (offset + 4 <= p.size()) {
        const std::size_t length = ((byte(p, offset + 2) << 8) | byte(p, offset + 3)) * 4 + 4;
        if (!lorica::contains_range(p.size(), offset, length)) break;  // truncated compound packet
        const unsigned type = byte(p, offset + 1), fmt = byte(p, offset) & 31;
        if (type == 206) {
            if ((fmt == 1 && length >= 12) || (fmt == 4 && length >= 20)) out.keyframe_request = true;
            if (fmt == 15 && length >= 24 && p.substr(offset + 12, 4) == "REMB") {
                const unsigned exponent = byte(p, offset + 17) >> 2;
                const std::uint64_t mantissa = ((byte(p, offset + 17) & 3u) << 16) | (byte(p, offset + 18) << 8) | byte(p, offset + 19);
                // A corrupt exponent cannot overflow: 18-bit mantissa << 63 is capped.
                out.remb_bps = exponent >= 46 ? UINT64_MAX : (mantissa << exponent);
            }
        }
        offset += length;
    }
    return out;
}

bool rewrite_rtp_header(std::string& packet, int payload_type, std::uint32_t ssrc) {
    if (packet.size() < 12 || (byte(packet, 0) >> 6) != 2) return false;
    packet[1] = static_cast<char>((byte(packet, 1) & 0x80) | (payload_type & 0x7f));
    for (int i = 0; i < 4; ++i) packet[8 + i] = static_cast<char>((ssrc >> (24 - 8 * i)) & 0xff);
    return true;
}
}
