#pragma once
// Browser feedback on the outgoing video (what werift exposed as onPictureLossIndication and
// receiverEstimatedMaxBitrate): PLI / FIR and REMB in compound RTCP, with every length checked.
// Plus the per-session RTP header rewrite (payload type and SSRC) werift's writeRtp performed.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace odeum::runner {
struct RtcpFeedback {
    bool keyframe_request = false;    // PLI (PSFB FMT 1) or FIR (PSFB FMT 4)
    std::optional<std::uint64_t> remb_bps;  // REMB (PSFB FMT 15 "REMB")
};
RtcpFeedback parse_rtcp_feedback(std::string_view packet);
bool is_rtcp(std::string_view packet);
// false when the packet is shorter than an RTP header or not version 2.
bool rewrite_rtp_header(std::string& packet, int payload_type, std::uint32_t ssrc);
}
