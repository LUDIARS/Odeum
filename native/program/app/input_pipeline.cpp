#include "input_pipeline.hpp"
#include <chrono>
#include <utility>

namespace odeum::program {
namespace {
constexpr std::size_t max_queued_units = 8;
}

std::int64_t steady_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

InputPipeline::InputPipeline(int input, std::unique_ptr<VideoDecoder> decoder, AudioMixer& mixer, KeyframeRequest keyframe)
    : input_(input), decoder_(std::move(decoder)), mixer_(mixer), keyframe_(std::move(keyframe)), worker_([this] { run(); }) {}

InputPipeline::~InputPipeline() {
    {
        std::lock_guard lock(mutex_);
        running_ = false;
    }
    wake_.notify_all();
    worker_.join();
}

void InputPipeline::video(std::span<const std::byte> access_unit) {
    bool overflow = false;
    {
        std::lock_guard lock(mutex_);
        queue_.emplace_back(access_unit.begin(), access_unit.end());
        if (queue_.size() > max_queued_units) {
            queue_.clear();
            overflow = true;
        }
    }
    if (overflow && keyframe_) keyframe_();
    wake_.notify_one();
}

void InputPipeline::audio(std::span<const std::byte> packet) {
    const auto pcm = opus_.decode(packet);
    std::lock_guard lock(audio_mutex_);
    pending_audio_.insert(pending_audio_.end(), pcm.begin(), pcm.end());
    std::size_t at = 0;
    for (; pending_audio_.size() - at >= mix_frame_floats; at += mix_frame_floats)
        mixer_.push(input_, std::span(pending_audio_).subspan(at, mix_frame_floats));
    pending_audio_.erase(pending_audio_.begin(), pending_audio_.begin() + static_cast<std::ptrdiff_t>(at));
}

void InputPipeline::reset() {
    {
        std::lock_guard lock(mutex_);
        queue_.clear();
        latest_.reset();
        reset_requested_ = true;
    }
    {
        std::lock_guard lock(audio_mutex_);
        pending_audio_.clear();
    }
    mixer_.clear(input_);
    wake_.notify_one();
}

std::optional<Nv12Picture> InputPipeline::latest(std::int64_t now_us, std::int64_t max_age_ms) const {
    std::lock_guard lock(mutex_);
    if (!latest_ || now_us - latest_->timestamp_us > max_age_ms * 1000) return std::nullopt;
    return latest_;
}

void InputPipeline::run() {
    for (;;) {
        std::vector<std::byte> unit;
        bool reset = false;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return !running_ || !queue_.empty() || reset_requested_; });
            if (!running_) return;
            reset = std::exchange(reset_requested_, false);
            if (!queue_.empty()) {
                unit = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        if (reset) decoder_->reset();
        if (unit.empty()) continue;
        try {
            for (auto& picture : decoder_->decode(unit, steady_us())) {
                // Stamped with the time it became ready, which is what freshness is judged by.
                picture.timestamp_us = steady_us();
                std::lock_guard lock(mutex_);
                latest_ = std::move(picture);
            }
        } catch (const std::exception&) {
            // A broken stream: start over from the next IDR.
            decoder_->reset();
            if (keyframe_) keyframe_();
        }
    }
}
}
