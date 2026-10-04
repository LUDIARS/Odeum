#include "overlay_state.hpp"
#include <algorithm>

namespace odeum::presenter {
OverlayState::OverlayState(OverlayLimits limits) : limits_(limits) {}

void OverlayState::push(std::deque<TimedReaction>& list, std::size_t limit, Millis lifetime, const Json& body,
                        const char* field, Millis now) {
    const auto& from = body.at("from");
    list.push_back({body.at(field).get<std::string>(), from.at("sub").get<std::string>(), from.at("name").get<std::string>(),
                    now, now + lifetime});
    while (list.size() > limit) list.pop_front();
}

void OverlayState::counts(const Json& counts, std::int64_t answered) {
    if (!poll_) return;
    poll_->counts = counts.get<std::vector<std::int64_t>>();
    poll_->counts.resize(poll_->choices.empty() ? poll_->counts.size() : poll_->choices.size(), 0);
    poll_->answered = answered;
}

bool OverlayState::apply(const Message& message, Millis now) {
    const auto& body = message.body;
    switch (message.type) {
    case MessageType::welcome:
        self_name_ = body.at("self").at("name").get<std::string>();
        session_id_ = body.at("sid").get<std::string>();
        last_error_.clear();
        return true;
    case MessageType::reaction_burst: {
        const auto good = body.at("good").get<std::int64_t>();
        if (good > 0) {
            good_total_ += good;
            bursts_.push_back({good, now});
        }
        for (const auto& [kind, n] : body.at("stamps").items()) stamp_totals_[kind] += n.get<std::int64_t>();
        return true;
    }
    case MessageType::stamp:
        // Only the presenter's copy names who pressed it; a nameless one has nothing to show.
        if (!body.contains("from")) return false;
        push(stamps_, limits_.stamp_limit, limits_.stamp_lifetime, body, "kind", now);
        return true;
    case MessageType::comment:
        if (!body.contains("from")) return false;
        push(comments_, limits_.comment_limit, limits_.comment_lifetime, body, "text", now);
        return true;
    case MessageType::poll_open: {
        PollTally poll;
        poll.poll_id = body.at("poll_id").get<std::string>();
        poll.question = body.at("question").get<std::string>();
        poll.choices = body.at("choices").get<std::vector<std::string>>();
        poll.multi = body.at("multi").get<bool>();
        poll.counts.assign(poll.choices.size(), 0);
        poll.open = true;
        poll_ = std::move(poll);
        return true;
    }
    case MessageType::tally:
        if (!poll_ || poll_->poll_id != body.at("poll_id").get<std::string>()) return false;
        counts(body.at("counts"), body.at("answered").get<std::int64_t>());
        return true;
    case MessageType::poll_closed:
        if (!poll_ || poll_->poll_id != body.at("poll_id").get<std::string>()) return false;
        counts(body.at("tally").at("counts"), body.at("tally").at("answered").get<std::int64_t>());
        poll_->open = false;
        poll_->closed_at = now;
        return true;
    case MessageType::presence:
        presenter_connected_ = body.at("presenter_connected").get<bool>();
        viewer_count_ = body.at("viewer_count").get<std::int64_t>();
        return true;
    case MessageType::error:
        last_error_ = body.at("code").get<std::string>();
        return true;
    default:
        return false;
    }
}

void OverlayState::expire(Millis now) {
    const auto gone = [now](const TimedReaction& r) { return r.expires_at <= now; };
    std::erase_if(stamps_, gone);
    std::erase_if(comments_, gone);
    std::erase_if(bursts_, [&](const GoodBurst& b) { return now - b.at >= limits_.burst_lifetime; });
    if (poll_ && !poll_->open && now - poll_->closed_at >= limits_.closed_poll_lifetime) poll_.reset();
}

bool OverlayState::animating() const noexcept {
    return !bursts_.empty() || !stamps_.empty() || !comments_.empty() || (poll_ && !poll_->open);
}

double OverlayState::heat(Millis now) const {
    std::int64_t recent = 0;
    for (const auto& burst : bursts_) if (now - burst.at < limits_.heat_window) recent += burst.count;
    const double per_second = static_cast<double>(recent) * 1000.0 / static_cast<double>(limits_.heat_window);
    return std::clamp(per_second / limits_.full_heat_per_second, 0.0, 1.0);
}

void OverlayState::reset() {
    good_total_ = 0;
    bursts_.clear();
    stamp_totals_.clear();
    stamps_.clear();
    comments_.clear();
    poll_.reset();
    viewer_count_ = 0;
    presenter_connected_ = false;
    self_name_.clear();
    session_id_.clear();
    last_error_.clear();
}
}
