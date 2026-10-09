#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace odeum::program {
enum class FlvTagType : std::uint8_t { audio = 8, video = 9, script = 18 };

// One FLV tag: the body is exactly what an RTMP audio/video/data message carries, so the same
// tag goes to YouTube over RTMP and into a .flv file.
struct FlvTag {
    FlvTagType type{};
    std::uint32_t timestamp_ms{};
    std::vector<std::byte> body;
    bool operator==(const FlvTag&) const = default;
};

struct AvcParameterSets {
    std::vector<std::byte> sps, pps;
    bool operator==(const AvcParameterSets&) const = default;
};

struct StreamMetadata {
    int width{}, height{}, fps{};
    int video_kbps{}, audio_kbps{};
    int audio_rate{}, audio_channels{};
};

// The first SPS and PPS in an Annex-B access unit (3- or 4-byte start codes), if both are there.
std::optional<AvcParameterSets> find_parameter_sets(std::span<const std::byte> annexb);

// "FLV" header (version 1, audio+video flags) followed by PreviousTagSize0.
std::vector<std::byte> flv_header();
// 11-byte tag header, body, then the 4-byte size of the tag just written.
std::vector<std::byte> flv_serialize(const FlvTag& tag);

// onMetaData as an RTMP data message (`@setDataFrame`, `onMetaData`, ECMA array), the form
// YouTube's ingest reads before the first frame.
FlvTag metadata_tag(const StreamMetadata& metadata);
// AVC sequence header: an AVCDecoderConfigurationRecord with 4-byte NAL lengths. High-family
// profiles carry the 4:2:0 8-bit extension. Throws std::invalid_argument for an SPS under 4 bytes.
FlvTag avc_sequence_header(const AvcParameterSets& sets);
// One access unit as AVCC (4-byte lengths). Access unit delimiters, SPS and PPS are left out
// (the sequence header carries them). `composition_ms` is PTS - DTS (B-frames).
FlvTag avc_frame(std::span<const std::byte> annexb, bool keyframe, std::uint32_t decode_ms, std::int32_t composition_ms);
// AAC sequence header carrying the AudioSpecificConfig of AAC-LC at this rate and channel
// count. Throws std::invalid_argument for a rate AAC has no index for or channels outside 1..2.
FlvTag aac_sequence_header(int sample_rate, int channels);
// One raw AAC frame (no ADTS header).
FlvTag aac_frame(std::span<const std::byte> raw, std::uint32_t timestamp_ms);
}
