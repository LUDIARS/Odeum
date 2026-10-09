#include "program_engine.hpp"
#include <algorithm>
#include <chrono>

namespace odeum::program {
namespace {
constexpr std::int64_t frame_us = 1000000 / program_fps, audio_us = 20000;
// A picture older than this is a stalled input: the plate replaces it.
constexpr std::int64_t max_picture_age_ms = 1000;
// Further behind than this, the clock skips ahead instead of bursting frames.
constexpr std::int64_t max_lag_us = 200000;

presenter::StreamSettings stream(int width, int height, int kbps, int keyframe_s) {
    presenter::StreamSettings s;
    s.width = width;
    s.height = height;
    s.fps = program_fps;
    s.max_bitrate_kbps = kbps;
    s.keyframe_interval_s = keyframe_s;
    return s;
}
}

ProgramEngine::ProgramEngine(ProgramPlatform& platform, const ProgramSettings& settings, std::array<InputPipeline*, input_count> inputs,
                             AudioMixer& mixer, Outputs outputs)
    : inputs_(inputs), mixer_(mixer), outputs_(std::move(outputs)), renderer_(make_program_renderer(program_width, program_height)),
      program_encoder_(platform.video_encoder()), return_encoder_(platform.video_encoder()), aac_(platform.aac_encoder()),
      return_frame_(return_width, return_height) {
    program_encoder_->options({settings.profile, settings.b_frames});
    program_encoder_->configure(stream(program_width, program_height, settings.video_bitrate_kbps, settings.keyframe_interval_s),
                                [this](presenter::EncodedFrame frame) { if (outputs_.program_video) outputs_.program_video(frame); });
    // The relay forwards the return feed to browsers and Cocoiru unchanged: Constrained Baseline.
    return_encoder_->options({presenter::H264Profile::constrained_baseline, 0});
    return_encoder_->configure(stream(return_width, return_height, settings.return_bitrate_kbps, 2),
                               [this](presenter::EncodedFrame frame) { if (outputs_.return_video) outputs_.return_video(frame); });
    aac_->configure(settings.audio_bitrate_kbps);
    opus_.configure(settings.return_audio_kbps);
    worker_ = std::thread([this] { run(); });
}

ProgramEngine::~ProgramEngine() {
    running_.store(false);
    worker_.join();
    program_encoder_->stop();
    return_encoder_->stop();
}

void ProgramEngine::update(EngineSnapshot snapshot) {
    std::lock_guard lock(mutex_);
    if (snapshot.returning && !snapshot_.returning) return_keyframe_.store(true);
    snapshot_ = std::move(snapshot);
}

void ProgramEngine::video_tick(std::int64_t now_us, const EngineSnapshot& snapshot) {
    CompositionInput input;
    input.layout = layout(snapshot.scene, snapshot.connected, program_width, program_height);
    for (int i = 0; i < input_count; ++i)
        if (inputs_[static_cast<std::size_t>(i)]) input.pictures[static_cast<std::size_t>(i)] = inputs_[static_cast<std::size_t>(i)]->latest(now_us, max_picture_age_ms);
    input.layers = snapshot.layers;
    const auto& composed = renderer_->compose(input);
    presenter::VideoFrame frame;
    frame.width = composed.width;
    frame.height = composed.height;
    frame.timestamp_us = now_us;
    frame.nv12 = std::make_shared<const std::vector<std::uint8_t>>(composed.pixels);
    program_encoder_->encode(frame, program_keyframe_.exchange(false));
    if (snapshot.returning) {
        scale_into(composed, return_frame_);
        presenter::VideoFrame small;
        small.width = return_frame_.width;
        small.height = return_frame_.height;
        small.timestamp_us = now_us;
        small.nv12 = std::make_shared<const std::vector<std::uint8_t>>(return_frame_.pixels);
        return_encoder_->encode(small, return_keyframe_.exchange(false));
    }
    ++frames_;
}

void ProgramEngine::audio_tick(std::int64_t timestamp_us, const EngineSnapshot& snapshot) {
    const auto pcm = mixer_.mix(snapshot.scene, snapshot.muted, snapshot.volume);
    if (outputs_.program_audio) for (const auto& packet : aac_->encode(pcm, timestamp_us)) outputs_.program_audio(packet);
    if (snapshot.returning && outputs_.return_audio) outputs_.return_audio(opus_.encode(pcm), timestamp_us);
}

void ProgramEngine::run() {
    auto next_video = steady_us(), next_audio = next_video;
    while (running_.load()) {
        const auto now = steady_us();
        EngineSnapshot snapshot;
        {
            std::lock_guard lock(mutex_);
            snapshot = snapshot_;
        }
        try {
            if (now >= next_audio) {
                audio_tick(next_audio, snapshot);
                next_audio += audio_us;
                if (now - next_audio > max_lag_us) next_audio = now;
            }
            if (now >= next_video) {
                video_tick(now, snapshot);
                next_video += frame_us;
                if (now - next_video > max_lag_us) { next_video = now; ++late_; }
            }
        } catch (const std::exception&) {
            // One failed frame (an encoder hiccup) must not stop the programme.
            ++late_;
            next_video = now + frame_us;
        }
        const auto wake = std::min(next_video, next_audio) - steady_us();
        if (wake > 0) std::this_thread::sleep_for(std::chrono::microseconds(wake));
    }
}
}
