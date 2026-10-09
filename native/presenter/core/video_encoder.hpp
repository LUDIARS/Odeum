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
    // Decode order time; differs from timestamp_us only when B-frames reorder the output.
    std::int64_t decode_timestamp_us{};
    bool keyframe{};
};

enum class H264Profile { constrained_baseline, main, high };

// Profile and B-frames. The defaults are what the presenter streams over WebRTC
// (packetization-mode=1, no reordering); odeum-program asks for High with B-frames for YouTube.
struct H264Options {
    H264Profile profile = H264Profile::constrained_baseline;
    // 0..2. An encoder that cannot produce B-frames ignores this and emits none.
    int b_frames = 0;
    bool operator==(const H264Options&) const = default;
};

// encode() may be called from the capture thread; the sink is called on whichever thread
// produced the output.
class VideoEncoder {
public:
    using Sink = std::function<void(EncodedFrame)>;
    virtual ~VideoEncoder() = default;
    // Takes effect at the next configure().
    void options(const H264Options& options) noexcept { h264_ = options; }
    const H264Options& options() const noexcept { return h264_; }
    virtual void configure(const StreamSettings&, Sink) = 0;
    // force_keyframe: this frame must come out as an IDR.
    virtual void encode(const VideoFrame&, bool force_keyframe) = 0;
    virtual void stop() = 0;
protected:
    H264Options h264_;
};
}
