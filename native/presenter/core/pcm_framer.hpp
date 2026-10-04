#pragma once
#include "audio_source.hpp"
#include <functional>
#include <span>
#include <vector>

namespace odeum::presenter {
inline constexpr int pcm_rate = 48000, pcm_channels = 2, pcm_frame_samples = 960; // 20 ms

// Turns whatever the OS delivers into 48 kHz stereo 20 ms frames: mono is duplicated, extra
// channels beyond front left/right are dropped, other rates are resampled linearly. Keeps its
// position across blocks so frame boundaries never click.
class PcmFramer {
public:
    using FrameSink = std::function<void(std::span<const float> frame, std::int64_t timestamp_us)>;
    void push(const AudioBlock&, const FrameSink&);
    void reset();
private:
    std::vector<float> pending_; // 48 kHz stereo, interleaved
    double position_ = 0;        // source position of the next output sample, relative to last_
    float last_[2]{};            // last source sample of the previous block, for interpolation
    bool primed_ = false;
    int rate_ = 0;
    std::int64_t next_timestamp_us_ = -1;
};
}
