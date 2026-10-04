#pragma once
#include "capture_source.hpp"
#include <cstddef>
#include <functional>
#include <vector>

namespace odeum::presenter {
// H.264 access unit in Annex-B form (start codes), parameter sets included on every IDR.
struct EncodedFrame {
    std::vector<std::byte> data;
    std::int64_t timestamp_us{};
    bool keyframe{};
};

// H.264 Constrained Baseline, packetization-mode=1 friendly (no B-frames, no reordering).
// encode() may be called from the capture thread; the sink is called on whichever thread
// produced the output.
class VideoEncoder {
public:
    using Sink = std::function<void(EncodedFrame)>;
    virtual ~VideoEncoder() = default;
    virtual void configure(const StreamSettings&, Sink) = 0;
    // force_keyframe: this frame must come out as an IDR.
    virtual void encode(const VideoFrame&, bool force_keyframe) = 0;
    virtual void stop() = 0;
};
}
