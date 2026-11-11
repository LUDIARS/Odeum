#include "timeline_player.hpp"
#include <algorithm>
#include <array>

namespace odeum::lesson {
void TimelinePlayer::start(Millis now) {
    if (state_ != State::idle) throw ProtocolError("invalid_state", "Lesson already started");
    state_ = State::running; origin_ = now; next_ = 0;
}
void TimelinePlayer::pause(Millis now) {
    if (state_ != State::running) throw ProtocolError("invalid_state", "Lesson is not running");
    held_ = position(now); state_ = State::paused;
}
void TimelinePlayer::resume(Millis now) {
    if (state_ != State::paused) throw ProtocolError("invalid_state", "Lesson is not paused");
    origin_ = now - held_; state_ = State::running;
}
void TimelinePlayer::seek(Millis target, Millis now) {
    if (state_ == State::idle) throw ProtocolError("invalid_state", "Lesson has not started");
    target = std::clamp<Millis>(target, 0, timeline_.duration);
    next_ = static_cast<std::size_t>(std::lower_bound(timeline_.cues.begin(), timeline_.cues.end(), target,
        [](const Cue& cue, Millis at) { return cue.at < at; }) - timeline_.cues.begin());
    held_ = target; origin_ = now - target;
    if (state_ == State::finished) state_ = State::paused;
}
bool TimelinePlayer::skip_section(Millis now) {
    if (state_ == State::idle) throw ProtocolError("invalid_state", "Lesson has not started");
    const auto here = position(now);
    const auto found = std::find_if(timeline_.cues.begin(), timeline_.cues.end(),
        [here](const Cue& cue) { return cue.kind == CueKind::section_start && cue.at > here; });
    if (found == timeline_.cues.end()) return false;
    seek(found->at, now);
    return true;
}
std::vector<Cue> TimelinePlayer::due(Millis now) {
    std::vector<Cue> out;
    if (state_ != State::running) return out;
    const auto here = position(now);
    while (next_ < timeline_.cues.size() && timeline_.cues[next_].at <= here) out.push_back(timeline_.cues[next_++]);
    if (next_ == timeline_.cues.size()) { state_ = State::finished; held_ = timeline_.duration; }
    return out;
}
Millis TimelinePlayer::position(Millis now) const {
    if (state_ == State::idle) return 0;
    if (state_ != State::running) return held_;
    return std::clamp<Millis>(now - origin_, 0, timeline_.duration);
}
std::string_view player_state_name(TimelinePlayer::State state) {
    constexpr std::array names{"idle", "running", "paused", "finished"};
    return names.at(static_cast<std::size_t>(state));
}
}
