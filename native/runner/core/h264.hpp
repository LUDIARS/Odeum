#pragma once
// H.264 without ffmpeg (custos/src/capture/h264-rtp-packetizer.ts, bridge-h264-frames.ts):
// the Unity bridge's framed Annex-B stream and RFC 6184 packetization (mode 1, PT 96, MTU 1200).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace odeum::runner {
inline constexpr std::size_t rtp_mtu = 1200;
inline constexpr std::size_t max_bridge_frame_bytes = 16u * 1024 * 1024;

// NAL units (without start codes); trailing zero bytes belong to the next start code.
std::vector<std::string_view> split_annex_b(std::string_view data);

class H264RtpPacketizer {
public:
    H264RtpPacketizer(int payload_type, std::size_t mtu, std::uint16_t initial_sequence, std::uint32_t ssrc);
    // One access unit -> RTP packets (single NAL or FU-A); the last carries the marker bit.
    std::vector<std::string> packetize(std::string_view access_unit, std::int64_t timestamp_us);
private:
    int payload_type_;
    std::size_t mtu_;
    std::uint16_t sequence_;
    std::uint32_t ssrc_;
    std::string packet(std::uint32_t timestamp, std::string_view prefix, std::string_view payload);
};

struct BridgeH264Frame {
    std::string data;  // Annex-B access unit
    std::int64_t timestamp_us = 0;
    bool keyframe = false;
};

class FrameTooLarge : public std::exception {
public:
    const char* what() const noexcept override { return "bridge h264 frame too large"; }
};

// [u32 BE length][i64 BE timestamp µs][u8 flags (bit0 keyframe)] + payload; length 0 is a keepalive.
// HTTP chunk boundaries are unrelated to frames; at most one frame (16 MiB) is buffered.
class BridgeH264FrameReader {
public:
    std::vector<BridgeH264Frame> push(std::string_view chunk);  // throws FrameTooLarge
private:
    std::string buffer_;
};
}
