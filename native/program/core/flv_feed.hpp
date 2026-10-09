#pragma once
#include "flv.hpp"
#include "core/video_encoder.hpp"
#include <optional>

namespace odeum::program {
// Turns encoded programme video (Annex-B) and AAC packets into the FLV tag sequence YouTube
// wants, on a timeline that starts at 0 for every (re)connection: onMetaData, the AVC and AAC
// sequence headers, then frames from the first keyframe on. Until a keyframe arrives, video
// and audio are dropped so the stream never opens with frames a decoder cannot use.
class FlvFeed {
public:
    FlvFeed(StreamMetadata metadata, int aac_rate, int aac_channels);
    // A new connection: the next tags start again from the headers.
    void restart() noexcept;
    // Tags to send for one encoded frame (possibly none). Parameter sets are taken from the
    // keyframes.
    std::vector<FlvTag> video(const presenter::EncodedFrame& frame);
    // Tags for one raw AAC frame stamped at `timestamp_us` on the same clock as the video.
    std::vector<FlvTag> audio(std::span<const std::byte> raw, std::int64_t timestamp_us);
    bool started() const noexcept { return origin_us_.has_value(); }
private:
    std::uint32_t relative_ms(std::int64_t us) const;
    StreamMetadata metadata_;
    int aac_rate_, aac_channels_;
    std::optional<AvcParameterSets> sets_;
    std::optional<std::int64_t> origin_us_;
};
}
