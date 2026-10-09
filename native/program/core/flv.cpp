#include "flv.hpp"
#include "amf0.hpp"
#include "bytes.hpp"
#include <array>
#include <stdexcept>

namespace odeum::program {
namespace {
constexpr int nal_sps = 7, nal_pps = 8, nal_aud = 9;
// AAC in FLV: format 10, the "44 kHz" rate code, 16-bit, stereo. Decoders read the real rate
// and channel count from the AudioSpecificConfig.
constexpr std::uint32_t aac_audio_header = 0xAF;

// NAL units of an Annex-B stream without their start codes.
std::vector<std::span<const std::byte>> nal_units(std::span<const std::byte> data) {
    std::vector<std::span<const std::byte>> units;
    constexpr auto none = static_cast<std::size_t>(-1);
    std::size_t i = 0, start = none;
    const auto zero = std::byte{0}, one = std::byte{1};
    while (i + 2 < data.size()) {
        if (data[i] == zero && data[i + 1] == zero && data[i + 2] == one) {
            if (start != none) {
                auto end = i;
                while (end > start && data[end - 1] == zero) --end; // trailing zero of a 4-byte start code
                if (end > start) units.push_back(data.subspan(start, end - start));
            }
            i += 3;
            start = i;
        } else {
            ++i;
        }
    }
    if (start != none && start < data.size()) units.push_back(data.subspan(start));
    return units;
}

int nal_type(std::span<const std::byte> unit) { return unit.empty() ? -1 : std::to_integer<int>(unit[0]) & 0x1f; }

int aac_rate_index(int rate) {
    constexpr std::array rates{96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
    for (std::size_t i = 0; i < rates.size(); ++i) if (rates[i] == rate) return static_cast<int>(i);
    throw std::invalid_argument("AAC has no index for this sample rate");
}
}

std::optional<AvcParameterSets> find_parameter_sets(std::span<const std::byte> annexb) {
    AvcParameterSets sets;
    for (auto unit : nal_units(annexb)) {
        if (nal_type(unit) == nal_sps && sets.sps.empty()) sets.sps.assign(unit.begin(), unit.end());
        if (nal_type(unit) == nal_pps && sets.pps.empty()) sets.pps.assign(unit.begin(), unit.end());
    }
    if (sets.sps.empty() || sets.pps.empty()) return std::nullopt;
    return sets;
}

std::vector<std::byte> flv_header() {
    std::vector<std::byte> out;
    put_text(out, "FLV");
    put_u8(out, 1);    // version
    put_u8(out, 0x05); // audio + video
    put_u32(out, 9);   // header size
    put_u32(out, 0);   // PreviousTagSize0
    return out;
}

std::vector<std::byte> flv_serialize(const FlvTag& tag) {
    if (tag.body.size() > 0xffffff) throw std::invalid_argument("FLV tag body too large");
    std::vector<std::byte> out;
    out.reserve(tag.body.size() + 15);
    put_u8(out, static_cast<std::uint32_t>(tag.type));
    put_u24(out, static_cast<std::uint32_t>(tag.body.size()));
    put_u24(out, tag.timestamp_ms & 0xffffff);
    put_u8(out, tag.timestamp_ms >> 24); // TimestampExtended
    put_u24(out, 0);                     // StreamID
    put_bytes(out, tag.body);
    put_u32(out, static_cast<std::uint32_t>(tag.body.size() + 11));
    return out;
}

FlvTag metadata_tag(const StreamMetadata& m) {
    const auto number = [](int v) { return Amf0Value::of(static_cast<double>(v)); };
    FlvTag tag{FlvTagType::script, 0, {}};
    tag.body = amf0_encode({Amf0Value::of("@setDataFrame"), Amf0Value::of("onMetaData"), Amf0Value::ecma_array({
        {"width", number(m.width)}, {"height", number(m.height)}, {"framerate", number(m.fps)},
        {"videocodecid", number(7)}, {"videodatarate", number(m.video_kbps)},
        {"audiocodecid", number(10)}, {"audiodatarate", number(m.audio_kbps)},
        {"audiosamplerate", number(m.audio_rate)}, {"audiosamplesize", number(16)},
        {"stereo", Amf0Value::of(m.audio_channels == 2)}, {"encoder", Amf0Value::of("odeum-program")}})});
    return tag;
}

FlvTag avc_sequence_header(const AvcParameterSets& sets) {
    if (sets.sps.size() < 4 || sets.pps.empty()) throw std::invalid_argument("SPS/PPS missing or too short");
    if (sets.sps.size() > 0xffff || sets.pps.size() > 0xffff) throw std::invalid_argument("SPS/PPS too long");
    FlvTag tag{FlvTagType::video, 0, {}};
    auto& out = tag.body;
    put_u8(out, 0x17); // keyframe, AVC
    put_u8(out, 0);    // sequence header
    put_u24(out, 0);   // composition time
    put_u8(out, 1);    // configurationVersion
    put_u8(out, get_u8(sets.sps, 1)); // AVCProfileIndication
    put_u8(out, get_u8(sets.sps, 2)); // profile_compatibility
    put_u8(out, get_u8(sets.sps, 3)); // AVCLevelIndication
    put_u8(out, 0xFF); // 4-byte NAL lengths
    put_u8(out, 0xE1); // one SPS
    put_u16(out, static_cast<std::uint32_t>(sets.sps.size()));
    put_bytes(out, sets.sps);
    put_u8(out, 1);    // one PPS
    put_u16(out, static_cast<std::uint32_t>(sets.pps.size()));
    put_bytes(out, sets.pps);
    const auto profile = get_u8(sets.sps, 1);
    if (profile == 100 || profile == 110 || profile == 122 || profile == 144) {
        put_u8(out, 0xFC | 1); // chroma_format_idc 4:2:0
        put_u8(out, 0xF8);     // bit_depth_luma 8
        put_u8(out, 0xF8);     // bit_depth_chroma 8
        put_u8(out, 0);        // no SPS extensions
    }
    return tag;
}

FlvTag avc_frame(std::span<const std::byte> annexb, bool keyframe, std::uint32_t decode_ms, std::int32_t composition_ms) {
    FlvTag tag{FlvTagType::video, decode_ms, {}};
    auto& out = tag.body;
    out.reserve(annexb.size() + 16);
    put_u8(out, keyframe ? 0x17 : 0x27);
    put_u8(out, 1); // NALU
    put_u24(out, static_cast<std::uint32_t>(composition_ms) & 0xffffff);
    for (auto unit : nal_units(annexb)) {
        const auto type = nal_type(unit);
        if (type == nal_aud || type == nal_sps || type == nal_pps) continue;
        put_u32(out, static_cast<std::uint32_t>(unit.size()));
        put_bytes(out, unit);
    }
    return tag;
}

FlvTag aac_sequence_header(int sample_rate, int channels) {
    if (channels < 1 || channels > 2) throw std::invalid_argument("AAC channels must be 1 or 2");
    const auto index = static_cast<std::uint32_t>(aac_rate_index(sample_rate));
    // AudioSpecificConfig: 5 bits object type (2 = AAC-LC), 4 bits rate index, 4 bits channels, 3 bits zero.
    const std::uint32_t config = 2u << 11 | index << 7 | static_cast<std::uint32_t>(channels) << 3;
    FlvTag tag{FlvTagType::audio, 0, {}};
    put_u8(tag.body, aac_audio_header);
    put_u8(tag.body, 0); // sequence header
    put_u16(tag.body, config);
    return tag;
}

FlvTag aac_frame(std::span<const std::byte> raw, std::uint32_t timestamp_ms) {
    FlvTag tag{FlvTagType::audio, timestamp_ms, {}};
    tag.body.reserve(raw.size() + 2);
    put_u8(tag.body, aac_audio_header);
    put_u8(tag.body, 1); // raw
    put_bytes(tag.body, raw);
    return tag;
}
}
