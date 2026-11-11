#include "classroom.hpp"

namespace odeum::lesson {
Json Classroom::standing_json(const std::string& id, Millis now) const {
    const auto standing = conduct_.standing(id, now);
    Json out{{"type", "conduct"}, {"standing", standing_name(standing)}};
    // Clients have their own clocks, so a lock is sent as time remaining rather than as a deadline.
    if (standing == Conduct::Standing::locked) out["remaining_ms"] = conduct_.locked_until(id) - now;
    return out;
}
Json Classroom::welcome(const Student& to, Millis now) const {
    Json sections = Json::array();
    for (const auto& section : plot_.sections) sections.push_back({{"id", section.id}, {"title", section.title}});
    Json state{{"player", player_state_name(player_.state())}, {"section", section_ ? Json(section_->id) : Json(nullptr)},
        {"questions", questions_.shared_list(to.id)}, {"my_questions", questions_.mine(to.id)},
        {"my_reports", reports_.mine(to.id)}, {"conduct", standing_json(to.id, now)}};
    if (!slide_.is_null()) state["slide"] = slide_;
    if (!narration_.is_null()) state["narration"] = narration_;
    if (activity_) {
        state["activity"] = activity_->prompt();
        state["activity"]["answered"] = activity_->answered(to.id);
        if (activity_->answered(to.id)) state["activity"]["answer"] = activity_->submitted(to.id);
        if (activity_->state() == Activity::State::revealed) {
            state["activity_results"] = activity_->results();
            state["activity_personal"] = activity_->personal(to.id);
        }
    }
    if (assignment_ && assignment_active_) {
        state["assignment"] = assignment_->brief();
        state["assignment"]["done"] = assignment_->progress(to.id).at("done");
    }
    return {{"type", "welcome"}, {"student", to.id}, {"name", to.name}, {"resume", to.resume},
        {"lesson", {{"id", plot_.id}, {"title", plot_.title}, {"subtitle", plot_.subtitle}, {"engine", plot_.engine}, {"sections", std::move(sections)},
            {"tool", has_checked_steps(plot_)}}},
        {"state", std::move(state)}};
}
Json Classroom::tool_state(const std::string& id) const {
    const bool presenter = id.empty();
    Json out{{"role", presenter ? "presenter" : "student"}, {"class", {{"title", plot_.title}}}, {"features", plot_.tool_features},
        {"assignment", nullptr}};
    if (out["features"].is_null()) out["features"] = Json::object();
    if (presenter) { out["demo"] = {{"seq", demo_seq_}, {"states", demo_states_.empty() ? Json::object() : Json(demo_states_)}}; return out; }
    if (!assignment_ || !assignment_active_) return out;
    const auto done = assignment_->progress(id).at("done");
    Json steps = Json::array();
    for (std::size_t index = 0; index < assignment_->plan().steps.size(); ++index) {
        const auto& step = assignment_->plan().steps[index];
        Json entry{{"id", step.id}, {"label", step.label}, {"done", index < done.size() && done[index].get<bool>()}};
        if (step.check) entry["check"] = {{"variable", step.check->variable}, {"op", step.check->op}, {"value", step.check->value}};
        steps.push_back(std::move(entry));
    }
    out["assignment"] = {{"id", assignment_->plan().id}, {"title", assignment_->plan().title}, {"active", true}, {"steps", std::move(steps)}};
    return out;
}
Json Classroom::console_snapshot(Millis now) const {
    const auto progress = assignment_ ? assignment_->overview(now) : Json(nullptr);
    Json students = Json::array();
    for (const auto& [id, who] : students_) {
        const auto standing = conduct_.standing(id, now);
        Json entry{{"id", id}, {"name", who.name}, {"connected", who.connections > 0}, {"standing", standing_name(standing)},
            {"points", conduct_.points(id, now)}, {"speaking", voice_.speaking(id, now)},
            {"answered", activity_ && activity_->answered(id)}};
        if (standing == Conduct::Standing::locked) entry["remaining_ms"] = conduct_.locked_until(id) - now;
        if (!progress.is_null() && progress.at("students").contains(id)) entry["progress"] = progress.at("students").at(id);
        students.push_back(std::move(entry));
    }
    Json out{{"type", "snapshot"},
        {"class", {{"plot", plot_.id}, {"title", plot_.title}, {"entry_locked", entry_locked_}, {"model", model_->name()},
            {"demo_tool", !plot_.tool_features.empty()}}},
        {"player", {{"state", player_state_name(player_.state())}, {"position_ms", player_.position(now)},
            {"duration_ms", player_.timeline().duration}, {"section", section_ ? Json(section_->id) : Json(nullptr)}}},
        {"students", std::move(students)}, {"speakers", voice_.speakers(now)}, {"questions", questions_.console_list()},
        {"questions_waiting", questions_.waiting()}, {"reports", reports_.console_list()}, {"reports_open", reports_.open()}};
    if (activity_) {
        out["activity"] = activity_->prompt();
        out["activity"]["tally"] = activity_->state() == Activity::State::revealed ? activity_->results() : activity_->tally();
    }
    if (assignment_) out["assignment"] = {{"id", assignment_->plan().id}, {"title", assignment_->plan().title}, {"active", assignment_active_},
        {"steps", assignment_->brief().at("steps")}};
    return out;
}
Json Classroom::console_welcome(Millis now) const {
    Json activities = Json::array(), assignments = Json::array();
    for (const auto& section : plot_.sections) {
        if (section.activity) activities.push_back({{"id", section.activity->id}, {"section", section.id}, {"prompt", section.activity->prompt},
            {"kind", activity_kind_name(section.activity->kind)}});
        if (section.assignment) assignments.push_back({{"id", section.assignment->id}, {"section", section.id}, {"title", section.assignment->title}});
    }
    Json sections = Json::array();
    for (const auto& section : plot_.sections) sections.push_back({{"id", section.id}, {"title", section.title}});
    return {{"type", "console.welcome"}, {"timeline", timeline_json(player_.timeline())}, {"sections", std::move(sections)},
        {"activities", std::move(activities)}, {"assignments", std::move(assignments)}, {"snapshot", console_snapshot(now)}};
}
}
