#include "audio_mixer.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace odeum::program {
std::array<bool, input_count> audible_inputs(const Scene& scene, const std::array<bool, input_count>& muted) {
    std::array<bool, input_count> audible{};
    switch (scene.mode) {
    case SceneMode::full: audible[input_at(scene.focus)] = true; break;
    case SceneMode::quad: audible.fill(true); break;
    case SceneMode::standby:
    case SceneMode::ended: break;
    }
    for (std::size_t i = 0; i < audible.size(); ++i) audible[i] = audible[i] && !muted[i];
    return audible;
}

AudioMixer::AudioMixer(std::size_t max_queued) : max_queued_(std::max<std::size_t>(1, max_queued)) {}

void AudioMixer::push(int input, std::span<const float> frame) {
    if (frame.size() != mix_frame_floats) throw std::invalid_argument("The mixer takes one 20 ms stereo frame");
    std::lock_guard lock(mutex_);
    auto& queue = queues_[input_at(input)];
    queue.emplace_back(frame.begin(), frame.end());
    while (queue.size() > max_queued_) queue.pop_front();
}

void AudioMixer::clear(int input) {
    std::lock_guard lock(mutex_);
    queues_[input_at(input)].clear();
}

std::vector<float> AudioMixer::mix(const Scene& scene, const std::array<bool, input_count>& muted, double volume) {
    const auto audible = audible_inputs(scene, muted);
    std::vector<float> out(mix_frame_floats, 0.f);
    {
        std::lock_guard lock(mutex_);
        for (std::size_t i = 0; i < queues_.size(); ++i) {
            auto& queue = queues_[i];
            if (queue.empty()) continue;
            if (audible[i]) for (std::size_t s = 0; s < out.size(); ++s) out[s] += queue.front()[s];
            queue.pop_front();
        }
    }
    float peak = 0.f;
    const auto gain = static_cast<float>(std::clamp(volume, 0.0, 2.0));
    for (auto& sample : out) {
        sample *= gain;
        peak = std::max(peak, std::fabs(sample));
    }
    // Scaling the whole frame keeps the waveform's shape; hard clipping would add distortion.
    if (peak > 1.f) for (auto& sample : out) sample /= peak;
    return out;
}
}
