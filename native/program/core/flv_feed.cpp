#include "flv_feed.hpp"
#include <algorithm>

namespace odeum::program {
FlvFeed::FlvFeed(StreamMetadata metadata, int aac_rate, int aac_channels)
    : metadata_(metadata), aac_rate_(aac_rate), aac_channels_(aac_channels) {}

void FlvFeed::restart() noexcept { origin_us_.reset(); }

std::uint32_t FlvFeed::relative_ms(std::int64_t us) const {
    return static_cast<std::uint32_t>(std::max<std::int64_t>(0, (us - *origin_us_) / 1000));
}

std::vector<FlvTag> FlvFeed::video(const presenter::EncodedFrame& frame) {
    if (frame.keyframe) if (auto sets = find_parameter_sets(frame.data)) sets_ = std::move(sets);
    std::vector<FlvTag> tags;
    if (!origin_us_) {
        if (!frame.keyframe || !sets_) return tags;
        origin_us_ = frame.decode_timestamp_us;
        tags.push_back(metadata_tag(metadata_));
        tags.push_back(avc_sequence_header(*sets_));
        tags.push_back(aac_sequence_header(aac_rate_, aac_channels_));
    }
    const auto decode_ms = relative_ms(frame.decode_timestamp_us);
    const auto composition_ms = static_cast<std::int32_t>((frame.timestamp_us - frame.decode_timestamp_us) / 1000);
    tags.push_back(avc_frame(frame.data, frame.keyframe, decode_ms, composition_ms));
    return tags;
}

std::vector<FlvTag> FlvFeed::audio(std::span<const std::byte> raw, std::int64_t timestamp_us) {
    if (!origin_us_ || timestamp_us < *origin_us_) return {};
    return {aac_frame(raw, relative_ms(timestamp_us))};
}
}
