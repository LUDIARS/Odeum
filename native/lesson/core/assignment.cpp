#include "assignment.hpp"
#include <algorithm>
#include <numeric>

namespace odeum::lesson {
std::optional<std::size_t> AssignmentBoard::current_step(const Entry& entry) {
    const auto found = std::find(entry.done.begin(), entry.done.end(), false);
    if (found == entry.done.end()) return std::nullopt;
    return static_cast<std::size_t>(found - entry.done.begin());
}
bool AssignmentBoard::stalled(const Entry& entry, Millis now) const {
    return entry.started && current_step(entry) && now - entry.touched >= static_cast<Millis>(plan_->stall_seconds) * 1000;
}
void AssignmentBoard::enroll(const std::string& student, Millis now) {
    if (entries_.contains(student)) return;
    Entry entry;
    entry.done.assign(plan_->steps.size(), false);
    entry.hints.assign(plan_->steps.size(), 0);
    // Someone who joins late gets the full grace period from their own arrival.
    entry.touched = std::max(now, assigned_);
    entries_.emplace(student, std::move(entry));
}
void AssignmentBoard::withdraw(const std::string& student) { entries_.erase(student); }
void AssignmentBoard::report(const std::string& student, std::size_t step, bool done, Millis now) {
    const auto found = entries_.find(student);
    if (found == entries_.end()) throw ProtocolError("not_assigned", "No assignment for this student");
    if (step >= plan_->steps.size()) throw ProtocolError("invalid_step", "Unknown step");
    auto& entry = found->second;
    if (entry.done[step] == done) return;
    entry.done[step] = done;
    entry.started = true; entry.touched = now; entry.escalated = false;
    if (!done) entry.praised = false;
}
void AssignmentBoard::request_help(const std::string& student, Millis now) {
    const auto found = entries_.find(student);
    if (found == entries_.end()) throw ProtocolError("not_assigned", "No assignment for this student");
    auto& entry = found->second;
    if (entry.helped >= 0 && now - entry.helped < help_cooldown_ms) throw ProtocolError("rate_limited", "A hint is already on its way");
    entry.wants_help = true; entry.started = true;
}
AssignmentBoard::Intervention AssignmentBoard::next_hint(const std::string& student, Entry& entry, std::size_t step, Millis now) {
    const auto level = entry.hints[step];
    const auto available = std::max<std::size_t>(1, plan_->steps[step].hints.size());
    entry.touched = now; entry.helped = now;
    if (level >= available) {
        // Every prepared hint has been used: a person should look at this student's screen.
        entry.escalated = true;
        return {Kind::escalate, student, step, level, "用意したヒントを出し切りました。先生が直接見に行きます。少しだけ待っていてください。"};
    }
    ++entry.hints[step];
    return {Kind::hint, student, step, level, model_->hint(*plan_, step, level)};
}
std::vector<AssignmentBoard::Intervention> AssignmentBoard::monitor(Millis now) {
    std::vector<Intervention> out;
    // Many students stuck on the same step means the explanation was the problem, not the students:
    // say it once to everyone instead of repeating it privately.
    std::vector<std::size_t> stuck(plan_->steps.size());
    for (const auto& [student, entry] : entries_) if (stalled(entry, now) || entry.wants_help) if (const auto step = current_step(entry)) ++stuck[*step];
    for (std::size_t step = 0; step < stuck.size(); ++step) {
        const bool many = stuck[step] >= class_stall_minimum
            && static_cast<double>(stuck[step]) >= class_stall_share * static_cast<double>(entries_.size());
        if (!many || class_hinted_[step]) continue;
        class_hinted_[step] = true;
        out.push_back({Kind::class_hint, {}, step, 0, "「" + plan_->steps[step].label + "」で手が止まっている人が多いので、全員に向けて話します。" + model_->hint(*plan_, step, 0)});
        for (auto& [student, entry] : entries_) {
            if (current_step(entry) != step || !(stalled(entry, now) || entry.wants_help)) continue;
            entry.touched = now; entry.helped = now; entry.wants_help = false;
            entry.hints[step] = std::max<std::size_t>(entry.hints[step], 1);
        }
    }
    for (auto& [student, entry] : entries_) {
        const auto step = current_step(entry);
        if (!step) {
            if (!entry.praised) {
                entry.praised = true; entry.wants_help = false;
                out.push_back({Kind::praise, student, plan_->steps.size(), 0,
                    plan_->extra.empty() ? "全部できました。お見事です。" : "全部できました。余裕があれば次に挑戦: " + plan_->extra});
            }
            continue;
        }
        if (entry.wants_help) { entry.wants_help = false; out.push_back(next_hint(student, entry, *step, now)); continue; }
        if (entry.escalated) continue;
        if (!entry.started) {
            if (!entry.nudged && now - entry.touched >= start_grace_ms) {
                entry.nudged = true; entry.touched = now;
                out.push_back({Kind::start_nudge, student, 0, 0, "まだ手を付けていないようです。最初の一歩は「" + plan_->steps[0].label + "」です。"});
            } else if (entry.nudged && now - entry.touched >= static_cast<Millis>(plan_->stall_seconds) * 1000) {
                out.push_back(next_hint(student, entry, 0, now));
            }
            continue;
        }
        if (stalled(entry, now)) out.push_back(next_hint(student, entry, *step, now));
    }
    return out;
}
Json AssignmentBoard::brief() const {
    Json steps = Json::array();
    // `auto`: a linked tool ticks this step by watching the student's game.
    for (const auto& step : plan_->steps) steps.push_back({{"id", step.id}, {"label", step.label}, {"auto", step.check.has_value()}});
    return {{"id", plan_->id}, {"title", plan_->title}, {"brief", plan_->brief}, {"minutes", plan_->minutes}, {"steps", std::move(steps)}};
}
Json AssignmentBoard::progress(const std::string& student) const {
    const auto found = entries_.find(student);
    if (found == entries_.end()) return {{"id", plan_->id}, {"done", Json::array()}};
    return {{"id", plan_->id}, {"done", found->second.done}};
}
Json AssignmentBoard::overview(Millis now) const {
    Json students = Json::object();
    for (const auto& [student, entry] : entries_) {
        const auto step = current_step(entry);
        const char* state = !step ? "done" : entry.escalated ? "needs_teacher" : stalled(entry, now) || entry.wants_help ? "stuck" : entry.started ? "working" : "not_started";
        students[student] = {{"done", entry.done}, {"state", state}, {"idle_ms", now - entry.touched},
            {"hints", std::accumulate(entry.hints.begin(), entry.hints.end(), std::size_t{0})}};
    }
    return {{"id", plan_->id}, {"title", plan_->title}, {"students", std::move(students)}};
}
}
