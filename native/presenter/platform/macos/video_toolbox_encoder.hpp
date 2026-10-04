#pragma once
#include "../../core/video_encoder.hpp"
#include <memory>

namespace odeum::presenter::macos {
// VideoToolbox H.264: Constrained Baseline, real-time, no frame reordering, average bitrate at
// 70 % of the ceiling with a hard data-rate limit at the ceiling. AVCC output is rewritten to
// Annex-B and every IDR carries SPS/PPS. kVTEncodeFrameOptionKey_ForceKeyFrame serves keyframe
// requests.
class VideoToolboxEncoder final : public VideoEncoder {
public:
    VideoToolboxEncoder();
    ~VideoToolboxEncoder() override;
    void configure(const StreamSettings&, Sink) override;
    void encode(const VideoFrame&, bool force_keyframe) override;
    void stop() override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
