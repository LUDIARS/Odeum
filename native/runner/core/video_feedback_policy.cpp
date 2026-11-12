#include "video_feedback_policy.hpp"
#include <algorithm>
#include <cmath>

namespace odeum::runner {
namespace {
constexpr double change_ratio = 0.15;
constexpr long long raise_hold_ms = 4'000;
constexpr long long keyframe_min_interval_ms = 500;
long long clamp(double v, long long min, long long max) {
    const double rounded = std::floor(v + 0.5);  // Math.round
    return std::max<long long>(min, std::min<long long>(max, static_cast<long long>(std::min(rounded, 9e15))));
}
}

VideoFeedbackPolicy::VideoFeedbackPolicy(BitrateLimits limits) : limits_(limits), current_(clamp(static_cast<double>(limits.initial), limits.min, limits.max)) {}

std::optional<long long> VideoFeedbackPolicy::update_estimate(const std::string& session_id, double bps, long long now) {
    if (!std::isfinite(bps) || bps <= 0) return std::nullopt;
    estimates_[session_id] = bps;
    return decide(now);
}

std::optional<long long> VideoFeedbackPolicy::remove_session(const std::string& session_id, long long now) {
    if (estimates_.erase(session_id) == 0) return std::nullopt;
    return decide(now);
}

bool VideoFeedbackPolicy::should_request_keyframe(long long now) {
    if (last_keyframe_at_ && now - *last_keyframe_at_ < keyframe_min_interval_ms) return false;
    last_keyframe_at_ = now;
    return true;
}

std::optional<long long> VideoFeedbackPolicy::decide(long long now) {
    if (estimates_.empty()) return std::nullopt;
    double lowest = estimates_.begin()->second;
    for (const auto& [id, value] : estimates_) lowest = std::min(lowest, value);
    const auto target = clamp(lowest, limits_.min, limits_.max);
    if (std::llabs(target - current_) < static_cast<double>(current_) * change_ratio) {
        raise_since_.reset();
        return std::nullopt;
    }
    if (target < current_) {
        raise_since_.reset();
        current_ = target;
        return target;
    }
    if (!raise_since_) {
        raise_since_ = now;
        return std::nullopt;
    }
    if (now - *raise_since_ < raise_hold_ms) return std::nullopt;
    raise_since_.reset();
    current_ = target;
    return target;
}
}
