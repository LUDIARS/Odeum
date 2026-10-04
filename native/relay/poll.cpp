#include "poll.hpp"
#include <set>

namespace odeum::relay {
void Poll::open(const Json& message, Millis now) {
    if (active_) throw ProtocolError("poll_active", "Close the current poll first");
    definition_ = parse_message(message.dump()).body;
    answers_.clear(); active_ = true; dirty_ = true; last_tally_ = now;
}
Json Poll::answer(const std::string& sub, const std::string& id, const std::vector<int>& choices) {
    if (!active_ || definition_.at("poll_id") != id) throw ProtocolError("poll_not_open", "Poll is not open");
    if (choices.empty() || choices.size() > definition_["choices"].size() || (!definition_["multi"].get<bool>() && choices.size() != 1))
        throw ProtocolError("invalid_answer", "Invalid choices");
    std::set<int> unique;
    for (int choice : choices) if (choice < 0 || static_cast<std::size_t>(choice) >= definition_["choices"].size() || !unique.insert(choice).second)
        throw ProtocolError("invalid_answer", "Invalid choices");
    if (!answers_.contains(sub) && answers_.size() >= 100000) throw ProtocolError("capacity", "Poll answer limit reached");
    answers_[sub] = choices; dirty_ = true; return tally();
}
Json Poll::tally() const {
    std::vector<int> counts(definition_.at("choices").size());
    for (const auto& [sub, choices] : answers_) for (auto choice : choices) ++counts.at(choice);
    return {{"type", "tally"}, {"poll_id", definition_.at("poll_id")}, {"counts", counts}, {"answered", answers_.size()}};
}
Json Poll::close(const std::string& id) {
    if (!active_ || definition_.at("poll_id") != id) throw ProtocolError("poll_not_open", "Poll is not open");
    active_ = false; dirty_ = false;
    return {{"type", "poll.closed"}, {"poll_id", id}, {"tally", tally()}};
}
std::optional<Json> Poll::flush(Millis now) {
    if (!active_ || !dirty_ || now - last_tally_ < 1000) return std::nullopt;
    dirty_ = false; last_tally_ = now; return tally();
}
std::optional<Json> Poll::current() const { return active_ ? std::optional<Json>(definition_) : std::nullopt; }
}
