#pragma once
#include "reconnect_backoff.hpp"
#include <odeum/message.hpp>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace odeum::presenter {
struct OverlayLimits {
    Millis stamp_lifetime = 4000;
    std::size_t stamp_limit = 12;
    Millis comment_lifetime = 15000;
    Millis telop_lifetime = 5000;
    std::size_t comment_limit = 8;
    // Bursts stay this long so their rising particles can finish.
    Millis burst_lifetime = 2500;
    // Window over which the good rate is measured, and the rate that counts as full heat.
    Millis heat_window = 3000;
    double full_heat_per_second = 40;
    // A closed poll's final tally stays on screen this long.
    Millis closed_poll_lifetime = 30000;
};

struct TimedReaction {
    std::string kind_or_text; // stamp kind, or the comment text
    std::string sub, name;
    Millis shown_at{}, expires_at{};
};

struct GoodBurst {
    std::int64_t count{};
    Millis at{};
};

struct PollTally {
    std::string poll_id, question;
    std::vector<std::string> choices;
    bool multi{};
    std::vector<std::int64_t> counts;
    std::int64_t answered{};
    bool open{};
    Millis closed_at{};
};

// What the desktop overlay shows, derived only from relay messages and the injected clock.
class OverlayState {
public:
    explicit OverlayState(OverlayLimits limits = {});
    // Applies one relay message; false when the message does not concern the overlay.
    bool apply(const Message& message, Millis now);
    // Drops stamps, comments, bursts and closed polls whose time is over.
    void expire(Millis now);
    // True while something on screen still moves or will disappear on its own.
    bool animating() const noexcept;
    // 0..1: how hard the audience is pressing good right now. Drives how lively the fountain is.
    double heat(Millis now) const;
    // Forgets everything tied to the previous connection (a new session starts from zero).
    void reset();

    std::int64_t good_total() const noexcept { return good_total_; }
    const std::deque<GoodBurst>& bursts() const noexcept { return bursts_; }
    const std::map<std::string, std::int64_t>& stamp_totals() const noexcept { return stamp_totals_; }
    const std::deque<TimedReaction>& stamps() const noexcept { return stamps_; }
    const std::deque<TimedReaction>& comments() const noexcept { return comments_; }
    const std::deque<TimedReaction>& telops() const noexcept { return telops_; }
    const std::optional<PollTally>& poll() const noexcept { return poll_; }
    std::int64_t viewer_count() const noexcept { return viewer_count_; }
    bool presenter_connected() const noexcept { return presenter_connected_; }
    const std::string& self_name() const noexcept { return self_name_; }
    const std::string& session_id() const noexcept { return session_id_; }
    const std::string& last_error() const noexcept { return last_error_; }
    const OverlayLimits& limits() const noexcept { return limits_; }
private:
    void push(std::deque<TimedReaction>& list, std::size_t limit, Millis lifetime, const Json& body, const char* field, Millis now);
    void counts(const Json& counts, std::int64_t answered);
    OverlayLimits limits_;
    std::int64_t good_total_ = 0;
    std::deque<GoodBurst> bursts_;
    std::map<std::string, std::int64_t> stamp_totals_;
    std::deque<TimedReaction> stamps_, comments_, telops_;
    std::optional<PollTally> poll_;
    std::int64_t viewer_count_ = 0;
    bool presenter_connected_ = false;
    std::string self_name_, session_id_, last_error_;
};
}
