#pragma once
#include "video_encoder.hpp"
#include <atomic>
#include <mutex>
#include <span>

namespace odeum::presenter {
// Capture -> encoder -> sender. Holds the frame-rate gate and turns keyframe requests that
// arrive over RTCP into a forced IDR on the next frame. The encoder is injected so tests can
// observe what it was asked to do without any OS codec.
class VideoPipeline {
public:
    using FrameOut = std::function<void(const EncodedFrame&)>;
    explicit VideoPipeline(VideoEncoder& encoder);
    ~VideoPipeline();
    VideoPipeline(const VideoPipeline&) = delete;
    VideoPipeline& operator=(const VideoPipeline&) = delete;
    // Configures the encoder; the first frame after start is always an IDR.
    void start(const StreamSettings&, FrameOut out);
    void stop();
    bool running() const noexcept { return running_; }
    // Capture thread. Frames closer together than the configured rate allows are dropped.
    void on_frame(const VideoFrame&);
    // Transport thread. True when the packet asked for a keyframe (the next frame will be one).
    bool on_rtcp(std::span<const std::byte> packet);
    void request_keyframe() noexcept { keyframe_.store(true); }
    std::uint64_t frames_encoded() const noexcept { return encoded_; }
    std::uint64_t keyframes_forced() const noexcept { return forced_; }
private:
    VideoEncoder& encoder_;
    std::mutex mutex_;
    std::atomic<bool> running_{false}, keyframe_{false};
    std::atomic<std::uint64_t> encoded_{0}, forced_{0};
    std::int64_t interval_us_ = 0, last_us_ = 0;
    bool first_ = true;
};
}
