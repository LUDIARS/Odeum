#pragma once
#include "timeline.hpp"

namespace odeum::lesson {
// Walks a timeline against a caller-supplied monotonic clock. It owns no timer: the classroom
// asks for due cues on every tick, which keeps playback deterministic under test.
class TimelinePlayer {
public:
    enum class State { idle, running, paused, finished };
    explicit TimelinePlayer(Timeline timeline) : timeline_(std::move(timeline)) {}
    void start(Millis now);
    void pause(Millis now);
    void resume(Millis now);
    // Jumps without replaying the cues in between. The classroom tolerates the resulting gaps
    // (for example a close cue for an activity that never opened).
    void seek(Millis position, Millis now);
    // Seeks to the next section.start after the current position; false when there is none.
    bool skip_section(Millis now);
    std::vector<Cue> due(Millis now);
    Millis position(Millis now) const;
    State state() const { return state_; }
    const Timeline& timeline() const { return timeline_; }
private:
    Timeline timeline_;
    State state_ = State::idle;
    Millis origin_ = 0;    // clock value that corresponds to position 0 while running
    Millis held_ = 0;      // position while paused or finished
    std::size_t next_ = 0;
};
std::string_view player_state_name(TimelinePlayer::State state);
}
