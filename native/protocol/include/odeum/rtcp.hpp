#pragma once
#include <cstddef>
#include <span>

namespace odeum {
// Largest RTP/RTCP packet Odeum accepts from a peer (one UDP datagram).
inline constexpr std::size_t max_rtp_packet_bytes = 65535;
// True when a (compound) RTCP packet carries a PLI or FIR. Every length is checked against the
// packet with Lorica before it is followed; malformed or oversized packets are not requests.
bool is_keyframe_request(std::span<const std::byte> packet);
}
