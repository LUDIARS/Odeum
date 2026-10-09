#pragma once
#include "program_state.hpp"
#include <deque>
#include <mutex>
#include <span>
#include <vector>

namespace odeum::program {
// 48 kHz stereo in 20 ms frames, the unit Opus decodes into and the mixer works in.
inline constexpr int mix_rate = 48000, mix_channels = 2, mix_frame_samples = 960;
inline constexpr std::size_t mix_frame_floats = static_cast<std::size_t>(mix_frame_samples * mix_channels);

// Which inputs the programme hears: the focused input when full screen, all of them in the
// grid, none on the standby and closing boards. Muted inputs are left out.
std::array<bool, input_count> audible_inputs(const Scene& scene, const std::array<bool, input_count>& muted);

// Mixes the decoded input audio into the programme sound. Inputs push from the network threads;
// the programme clock pulls one frame every 20 ms. Each input keeps at most `max_queued` frames
// (older ones are dropped so a stalled clock never grows latency), and an input with nothing
// queued contributes silence.
class AudioMixer {
public:
    explicit AudioMixer(std::size_t max_queued = 10);
    // Any thread. `frame` is one 20 ms stereo frame (mix_frame_floats floats).
    void push(int input, std::span<const float> frame);
    // Forgets what an input had queued (it left the relay).
    void clear(int input);
    // One 20 ms frame: audible inputs summed, times `volume`, then scaled down as a whole when a
    // sample would leave [-1, 1] so the programme never clips. Every input's queue advances by
    // one frame whether it is heard or not, keeping all inputs in step.
    std::vector<float> mix(const Scene& scene, const std::array<bool, input_count>& muted, double volume);
private:
    std::mutex mutex_;
    std::size_t max_queued_;
    std::array<std::deque<std::vector<float>>, input_count> queues_;
};
}
