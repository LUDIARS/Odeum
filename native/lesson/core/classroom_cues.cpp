#include "classroom.hpp"

namespace odeum::lesson {
namespace {
const char* intervention_name(AssignmentBoard::Kind kind) {
    switch (kind) {
        case AssignmentBoard::Kind::start_nudge: return "start_nudge";
        case AssignmentBoard::Kind::hint: return "hint";
        case AssignmentBoard::Kind::class_hint: return "class_hint";
        case AssignmentBoard::Kind::praise: return "praise";
        case AssignmentBoard::Kind::escalate: break;
    }
    return "escalate";
}
}

void Classroom::open_activity(Outbox& out, const std::string& id) {
    const auto plan = find_activity(plot_, id);
    if (!plan) throw ProtocolError("unknown_activity", "Unknown activity");
    close_activity(out);  // only one question is on screen at a time
    activity_.emplace(*plan, *model_);
    dirty_ = true;
    out.push_back(to_all({{"type", "activity.open"}, {"activity", activity_->prompt()}}));
}
void Classroom::close_activity(Outbox& out) {
    if (!activity_ || activity_->state() != Activity::State::open) return;
    activity_->close();
    dirty_ = true;
    out.push_back(to_all({{"type", "activity.closed"}, {"id", activity_->plan().id}}));
}
void Classroom::reveal_activity(Outbox& out) {
    if (!activity_ || activity_->state() == Activity::State::revealed) return;
    close_activity(out);
    activity_->reveal();
    dirty_ = true;
    out.push_back(to_all({{"type", "activity.reveal"}, {"results", activity_->results()}}));
    // Everyone sees the class result; each student also gets a word about their own answer.
    for (const auto& [id, who] : students_)
        if (who.connections > 0 && !conduct_.ejected(id)) out.push_back(to_student(id, {{"type", "activity.personal"}, {"result", activity_->personal(id)}}));
}
void Classroom::assign(Outbox& out, const std::string& id, Millis now) {
    const auto plan = find_assignment(plot_, id);
    if (!plan) throw ProtocolError("unknown_assignment", "Unknown assignment");
    assignment_.emplace(*plan, *model_, now);
    assignment_active_ = true;
    for (const auto& [student, who] : students_) if (who.connections > 0 && !conduct_.ejected(student)) assignment_->enroll(student, now);
    dirty_ = true;
    out.push_back(to_all({{"type", "assignment"}, {"assignment", assignment_->brief()}}));
}
void Classroom::close_assignment(Outbox& out) {
    if (!assignment_ || !assignment_active_) return;
    assignment_active_ = false;
    dirty_ = true;
    out.push_back(to_all({{"type", "assignment.closed"}, {"id", assignment_->plan().id}}));
}
void Classroom::share_questions(Outbox& out, const std::vector<std::string>& ids) {
    for (const auto& id : ids) {
        out.push_back(to_all({{"type", "question.shared"}, {"question", questions_.shared_view(id)}}));
        out.push_back(to_student(questions_.asker(id), {{"type", "reply"}, {"about", "question"}, {"question_id", id},
            {"text", "あなたの質問をクラス全体に共有しました。いい質問をありがとう。"}}));
    }
    if (!ids.empty()) dirty_ = true;
}
void Classroom::supervise(Outbox& out, Millis now) {
    if (!assignment_ || !assignment_active_) return;
    for (auto& intervention : assignment_->monitor(now)) {
        dirty_ = true;
        Json body{{"type", "hint"}, {"kind", intervention_name(intervention.kind)}, {"step", intervention.step},
            {"level", intervention.level}, {"text", intervention.text}};
        if (intervention.kind == AssignmentBoard::Kind::class_hint) { out.push_back(to_all(std::move(body))); continue; }
        if (intervention.kind == AssignmentBoard::Kind::escalate)
            out.push_back(to_consoles({{"type", "attention"}, {"student", intervention.student}, {"reason", "hints_exhausted"}, {"step", intervention.step}}));
        out.push_back(to_student(intervention.student, std::move(body)));
    }
}

void Classroom::run(Outbox& out, const Cue& cue, Millis now) {
    dirty_ = true;
    switch (cue.kind) {
        case CueKind::section_start: {
            // A seek can land here with the previous section's question still open.
            close_activity(out);
            section_ = find_section(plot_, cue.section);
            std::size_t index = 0;
            while (index < plot_.sections.size() && &plot_.sections[index] != section_) ++index;
            out.push_back(to_all({{"type", "section"}, {"id", cue.section}, {"title", cue.detail.at("title")},
                {"index", index}, {"total", plot_.sections.size()}}));
            return;
        }
        case CueKind::slide:
            slide_ = {{"type", "slide"}, {"section", cue.section}, {"title", cue.detail.at("title")}, {"points", cue.detail.at("points")}};
            out.push_back(to_all(slide_));
            return;
        case CueKind::narration:
            narration_ = {{"type", "narration"}, {"text", cue.detail.at("text")}, {"seconds", cue.detail.at("seconds")}};
            out.push_back(to_all(narration_));
            return;
        case CueKind::demo:
            out.push_back(to_all({{"type", "demo"}, {"feature", cue.detail.at("feature")}, {"enabled", cue.detail.at("enabled")}, {"note", cue.detail.at("note")}}));
            return;
        case CueKind::activity_open: open_activity(out, cue.detail.at("id")); return;
        case CueKind::activity_close: if (activity_ && cue.detail.at("id") == activity_->plan().id) close_activity(out); return;
        case CueKind::activity_reveal: if (activity_ && cue.detail.at("id") == activity_->plan().id) reveal_activity(out); return;
        case CueKind::assignment_assign: assign(out, cue.detail.at("id"), now); return;
        case CueKind::assignment_check: {
            if (!assignment_ || !assignment_active_) return;
            const auto overview = assignment_->overview(now);
            std::size_t done = 0;
            for (const auto& [id, entry] : overview.at("students").items()) done += entry.at("state") == "done";
            out.push_back(to_all({{"type", "assignment.status"}, {"done", done}, {"total", overview.at("students").size()}}));
            return;
        }
        case CueKind::assignment_close: if (assignment_ && cue.detail.at("id") == assignment_->plan().id) close_assignment(out); return;
        case CueKind::questions_share: share_questions(out, questions_.share_best()); return;
        case CueKind::section_end: return;
        case CueKind::lesson_end: out.push_back(to_all({{"type", "lesson.end"}})); return;
    }
}

Outbox Classroom::tick(Millis now) {
    Outbox out;
    for (const auto& cue : player_.due(now)) run(out, cue, now);
    for (const auto& id : conduct_.expire(now)) { dirty_ = true; out.push_back(to_student(id, standing_json(id, now))); }
    supervise(out, now);
    if (!voice_.settle(now).empty()) dirty_ = true;
    // The clock itself moves the console view while the lesson runs.
    if (player_.state() == TimelinePlayer::State::running) dirty_ = true;
    if (dirty_ && now - last_snapshot_ >= snapshot_interval_ms) {
        dirty_ = false; last_snapshot_ = now;
        out.push_back(to_consoles(console_snapshot(now)));
    }
    return out;
}
}
