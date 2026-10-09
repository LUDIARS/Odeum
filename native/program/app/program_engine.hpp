#pragma once
#include "input_pipeline.hpp"
#include "platform.hpp"
#include "../core/program_settings.hpp"
#include "../render/program_renderer.hpp"
#include "transport/opus_audio_encoder.hpp"
#include <array>

namespace odeum::program {
// What the UI thread hands the programme clock: the operator's choices and the text layers.
struct EngineSnapshot {
    Scene scene;
    std::array<bool, input_count> connected{}, muted{};
    double volume = 1.0;
    std::vector<Layer> layers;
    bool returning = false;
};

struct EngineStats {
    std::uint64_t frames{}, late_frames{};
};

// The programme clock on its own thread: every 1/30 s it composes the inputs and layers
// (ProgramRenderer), encodes the programme (H.264 High/Main, CBR, B-frames) and, while the
// return feed is on, scales it to 640x360 and encodes that as Constrained Baseline; every 20 ms
// it mixes the inputs' audio into AAC-LC for YouTube and Opus for the return feed.
class ProgramEngine {
public:
    struct Outputs {
        std::function<void(const presenter::EncodedFrame&)> program_video;
        std::function<void(const AacPacket&)> program_audio;
        std::function<void(const presenter::EncodedFrame&)> return_video;
        std::function<void(std::span<const std::byte> packet, std::int64_t timestamp_us)> return_audio;
    };
    ProgramEngine(ProgramPlatform& platform, const ProgramSettings& settings, std::array<InputPipeline*, input_count> inputs,
                  AudioMixer& mixer, Outputs outputs);
    ~ProgramEngine();
    ProgramEngine(const ProgramEngine&) = delete;
    ProgramEngine& operator=(const ProgramEngine&) = delete;
    // UI thread; the clock uses the newest snapshot from its next frame on.
    void update(EngineSnapshot snapshot);
    // Any thread: the next programme / return frame is an IDR.
    void request_program_keyframe() noexcept { program_keyframe_.store(true); }
    void request_return_keyframe() noexcept { return_keyframe_.store(true); }
    EngineStats stats() const noexcept { return {frames_.load(), late_.load()}; }
private:
    void run();
    void video_tick(std::int64_t now_us, const EngineSnapshot& snapshot);
    void audio_tick(std::int64_t timestamp_us, const EngineSnapshot& snapshot);
    std::array<InputPipeline*, input_count> inputs_;
    AudioMixer& mixer_;
    Outputs outputs_;
    std::unique_ptr<ProgramRenderer> renderer_;
    std::unique_ptr<presenter::VideoEncoder> program_encoder_, return_encoder_;
    std::unique_ptr<AacEncoder> aac_;
    presenter::OpusAudioEncoder opus_;
    Nv12Image return_frame_;
    std::mutex mutex_;
    EngineSnapshot snapshot_;
    std::atomic<bool> running_{true}, program_keyframe_{true}, return_keyframe_{true};
    std::atomic<std::uint64_t> frames_{0}, late_{0};
    std::thread worker_;
};
}
