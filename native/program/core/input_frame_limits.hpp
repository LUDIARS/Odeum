#pragma once
// Largest frames odeum-program takes from the relay's input slots (program/transport
// input_receiver.cpp). libdatachannel reassembles them from RTP; anything larger is dropped
// before it reaches the decoder, the mixer or a recording.
#include <odeum/rtcp.hpp>
#include <cstddef>

namespace odeum::program {
// One H.264 access unit: the same 8 MiB a recording's FLV tag may hold (archive_reader.cpp).
inline constexpr std::size_t max_input_video_frame_bytes = 8u << 20;
// One Opus packet arrives in one RTP packet, so it can never exceed one datagram.
inline constexpr std::size_t max_input_audio_frame_bytes = max_rtp_packet_bytes;

constexpr bool accepts_input_frame(bool video, std::size_t bytes) noexcept {
    return bytes > 0 && bytes <= (video ? max_input_video_frame_bytes : max_input_audio_frame_bytes);
}
}
