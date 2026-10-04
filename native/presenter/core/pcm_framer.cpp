#include "pcm_framer.hpp"
#include <stdexcept>

namespace odeum::presenter {
void PcmFramer::reset() {
    pending_.clear();
    position_ = 0;
    primed_ = false;
    rate_ = 0;
    next_timestamp_us_ = -1;
}

void PcmFramer::push(const AudioBlock& block, const FrameSink& sink) {
    if (block.channels < 1 || block.sample_rate < 8000 || block.sample_rate > 384000) throw std::invalid_argument("Unsupported PCM format");
    if (rate_ != block.sample_rate) { reset(); rate_ = block.sample_rate; }
    const auto frames = block.samples.size() / static_cast<std::size_t>(block.channels);
    if (frames == 0) return;
    if (next_timestamp_us_ < 0) next_timestamp_us_ = block.timestamp_us;
    const auto sample = [&](std::size_t i, int c) {
        const auto base = i * static_cast<std::size_t>(block.channels);
        return block.samples[base + static_cast<std::size_t>(block.channels == 1 ? 0 : c)];
    };
    if (!primed_) { last_[0] = sample(0, 0); last_[1] = sample(0, 1); primed_ = true; position_ = 1; }
    // Source index s (as a double) maps to: s < 1 -> between last_ and sample 0, else samples s-1 .. s.
    const double step = static_cast<double>(rate_) / pcm_rate;
    while (position_ <= static_cast<double>(frames)) {
        const auto whole = static_cast<std::size_t>(position_);
        const double fraction = position_ - static_cast<double>(whole);
        for (int c = 0; c < pcm_channels; ++c) {
            const float a = whole == 0 ? last_[c] : sample(whole - 1, c);
            const float b = whole >= frames ? a : sample(whole, c);
            pending_.push_back(a + static_cast<float>(fraction) * (b - a));
        }
        position_ += step;
    }
    position_ -= static_cast<double>(frames);
    last_[0] = sample(frames - 1, 0);
    last_[1] = sample(frames - 1, 1);
    const std::size_t frame_floats = pcm_frame_samples * pcm_channels;
    std::size_t consumed = 0;
    while (pending_.size() - consumed >= frame_floats) {
        sink(std::span<const float>(pending_).subspan(consumed, frame_floats), next_timestamp_us_);
        consumed += frame_floats;
        next_timestamp_us_ += 20000;
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(consumed));
}
}
