#pragma once
#include "../core/audio_mixer.hpp"
#include "../core/media_codecs.hpp"
#include "../transport/opus_audio_decoder.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace odeum::program {
// One input slot: H.264 access units from the network are decoded on the input's own thread
// (so a slow decoder never stalls libdatachannel), the newest picture is kept for the programme
// clock, and Opus packets are decoded straight into the mixer in 20 ms frames.
class InputPipeline {
public:
    using KeyframeRequest = std::function<void()>;
    InputPipeline(int input, std::unique_ptr<VideoDecoder> decoder, AudioMixer& mixer, KeyframeRequest keyframe);
    ~InputPipeline();
    InputPipeline(const InputPipeline&) = delete;
    InputPipeline& operator=(const InputPipeline&) = delete;
    // Network thread. When more than 8 access units are waiting, they are dropped and the
    // sender is asked for an IDR.
    void video(std::span<const std::byte> access_unit);
    void audio(std::span<const std::byte> packet);
    // The input left: forget its picture, queued data and decoder state.
    void reset();
    // The newest picture if it was decoded within `max_age_ms`; a stalled input has none, so
    // the programme shows the plate instead of a frozen frame.
    std::optional<Nv12Picture> latest(std::int64_t now_us, std::int64_t max_age_ms) const;
private:
    void run();
    const int input_;
    std::unique_ptr<VideoDecoder> decoder_;
    AudioMixer& mixer_;
    KeyframeRequest keyframe_;
    OpusAudioDecoder opus_;
    std::mutex audio_mutex_;
    std::vector<float> pending_audio_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::vector<std::byte>> queue_;
    std::optional<Nv12Picture> latest_;
    bool reset_requested_ = false, running_ = true;
    std::thread worker_;
};

// Microseconds on the steady clock, the time base of pictures and the programme clock.
std::int64_t steady_us();
}
