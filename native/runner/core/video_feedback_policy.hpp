#pragma once
// PLI coalescing and REMB-driven bitrate for one shared video source (video-feedback-policy.ts):
// the minimum of the viewers' estimates within [min, max]; down at once, up after 4 s; changes under
// 15 % are ignored; key frames at most every 500 ms.
#include <map>
#include <optional>
#include <string>

namespace odeum::runner {
struct BitrateLimits {
    long long initial, min, max;
};

class VideoFeedbackPolicy {
public:
    explicit VideoFeedbackPolicy(BitrateLimits limits);
    long long bitrate() const { return current_; }
    std::optional<long long> update_estimate(const std::string& session_id, double bps, long long now);
    std::optional<long long> remove_session(const std::string& session_id, long long now);
    bool should_request_keyframe(long long now);
private:
    BitrateLimits limits_;
    std::map<std::string, double> estimates_;
    long long current_;
    std::optional<long long> raise_since_;
    std::optional<long long> last_keyframe_at_;
    std::optional<long long> decide(long long now);
};
}
