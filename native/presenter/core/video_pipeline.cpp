#include "video_pipeline.hpp"
#include "keyframe_request.hpp"

namespace odeum::presenter {
VideoPipeline::VideoPipeline(VideoEncoder& encoder) : encoder_(encoder) {}
VideoPipeline::~VideoPipeline() { stop(); }

void VideoPipeline::start(const StreamSettings& settings, FrameOut out) {
    validate(settings);
    std::lock_guard lock(mutex_);
    if (running_) encoder_.stop();
    encoder_.configure(settings, [out = std::move(out)](EncodedFrame frame) { out(frame); });
    interval_us_ = 1000000 / settings.fps;
    first_ = true;
    keyframe_ = true;
    running_ = true;
}

void VideoPipeline::stop() {
    std::lock_guard lock(mutex_);
    if (!running_) return;
    running_ = false;
    encoder_.stop();
}

void VideoPipeline::on_frame(const VideoFrame& frame) {
    std::lock_guard lock(mutex_);
    if (!running_) return;
    // A tenth of slack so a source running at exactly the target rate is not halved by jitter.
    if (!first_ && frame.timestamp_us - last_us_ < interval_us_ - interval_us_ / 10) return;
    first_ = false;
    last_us_ = frame.timestamp_us;
    const bool force = keyframe_.exchange(false);
    if (force) ++forced_;
    encoder_.encode(frame, force);
    ++encoded_;
}

bool VideoPipeline::on_rtcp(std::span<const std::byte> packet) {
    if (!is_keyframe_request(packet)) return false;
    keyframe_ = true;
    return true;
}
}
