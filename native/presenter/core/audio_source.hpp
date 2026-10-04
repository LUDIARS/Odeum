#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace odeum::presenter {
// Interleaved float PCM as the OS delivers it, any rate and channel count.
struct AudioBlock {
    std::span<const float> samples;
    int channels{};
    int sample_rate{};
    std::int64_t timestamp_us{};
};

// The sound the audience should hear (the system mix, without the presenter app itself).
// Blocks arrive on the adapter's own thread.
class AudioSource {
public:
    using Sink = std::function<void(const AudioBlock&)>;
    using ErrorSink = std::function<void(const std::string&)>;
    virtual ~AudioSource() = default;
    virtual void start(Sink, ErrorSink) = 0;
    // Once stop() returns, no sink is called any more.
    virtual void stop() = 0;
};

// Opus wants 48 kHz stereo in 20 ms frames.
class AudioEncoder {
public:
    virtual ~AudioEncoder() = default;
    virtual void configure(int bitrate_kbps) = 0;
    // `frame` holds pcm_frame_samples stereo samples (interleaved, 2 * 960 floats).
    virtual std::vector<std::byte> encode(std::span<const float> frame) = 0;
};
}
