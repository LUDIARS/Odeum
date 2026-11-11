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
    revealed_.insert(activity_->plan().id);
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
// What the asker is told says why it was shared, in terms the rules can back: a prepared answer and
// the rule-based score (words of the question, "me too" count). Whether the answer satisfied the
// asker is not something the rules know, so it is not claimed.
void Classroom::share_questions(Outbox& out, const std::vector<std::string>& ids, bool by_operator) {
    for (const auto& id : ids) {
        const auto view = questions_.shared_view(id);
        out.push_back(to_all({{"type", "question.shared"}, {"question", view}}));
        const auto votes = std::to_string(questions_.votes(id));
        const std::string named = view.contains("name") ? "名前を付けて" : "名前を出さずに";
        const auto text = by_operator ? "先生が、あなたの質問を" + named + "クラス全体に共有しました。"
            : "あなたの質問と用意した答えを、" + named + "クラス全体に共有しました。答えが用意してあり、規則による評価 (質問の言葉と、同じことを知りたい人 "
                + votes + " 人) が共有の基準を超えたためです。答えであなたの疑問が解けたかどうかは、この判定では分かりません。"
                  "解けていなければ「あなたの質問」の「まだ分からない」を押してください。";
        out.push_back(to_student(questions_.asker(id), {{"type", "reply"}, {"about", "question"}, {"question_id", id}, {"relation", "asker"}, {"text", text}}));
        question_status(out, id);
    }
    if (!ids.empty()) dirty_ = true;
}
void Classroom::question_status(Outbox& out, const std::string& id) {
    for (const auto& student : questions_.audience(id))
        out.push_back(to_student(student, {{"type", "question.status"}, {"question", questions_.status_for(student, id)}}));
}
// The operator decides not to take a question up. Everyone waiting on it is told so, plainly.
void Classroom::dismiss_question(Outbox& out, const std::string& id) {
    const bool was_shared = questions_.knows(id) && questions_.shared(id);
    const auto recipients = questions_.dismiss(id);
    dirty_ = true;
    for (std::size_t i = 0; i < recipients.size(); ++i)
        out.push_back(to_student(recipients[i], {{"type", "reply"}, {"about", "question"}, {"question_id", id}, {"relation", i == 0 ? "asker" : "me_too"},
            {"text", "先生は「" + questions_.text(id) + "」を見送りにしました。この授業の中では、この質問への先生からの答えは届きません。"}}));
    question_status(out, id);
    if (was_shared) out.push_back(to_all({{"type", "question.removed"}, {"id", id}}));
}
// At the end of the lesson, nobody is left wondering whether an answer is still coming.
void Classroom::tell_unanswered(Outbox& out) {
    for (const auto& [id, who] : students_) {
        if (conduct_.ejected(id)) continue;
        const auto waiting = questions_.take_unanswered(id);
        if (waiting.empty()) continue;
        std::string list;
        for (const auto& question : waiting) list += "「" + questions_.text(question) + "」";
        out.push_back(to_student(id, {{"type", "reply"}, {"about", "questions_unanswered"}, {"count", waiting.size()},
            {"text", "授業の時間内に先生の答えが届かなかった質問が " + std::to_string(waiting.size()) + " 件あります: " + list
                + "。この授業の中では答えられませんでした。授業のあとに先生へ直接たずねてください。"}}));
    }
}
// The operator answers a question the model could not. The answer reaches the asker and everyone who
// said "me too", as the answer to that question; a shared question is updated for the whole class.
void Classroom::answer_question(Outbox& out, const std::string& id, const std::string& text) {
    const auto recipients = questions_.answer(id, text);
    dirty_ = true;
    for (std::size_t i = 0; i < recipients.size(); ++i)
        out.push_back(to_student(recipients[i], {{"type", "reply"}, {"about", "question"}, {"question_id", id}, {"relation", i == 0 ? "asker" : "me_too"},
            {"from", "teacher"}, {"text", "先生からの答え: " + text}}));
    question_status(out, id);
    if (questions_.shared(id)) out.push_back(to_all({{"type", "question.shared"}, {"question", questions_.shared_view(id)}}));
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

void Classroom::switch_demo(const std::string& feature, bool enabled) {
    if (feature == "all" && !plot_.tool_features.empty()) for (const auto& [name, unused] : plot_.tool_features) demo_states_[name] = enabled;
    else demo_states_[feature] = enabled;
}
// A seek jumps over cues without running them. What the class hears can simply resume from the
// new position, but the game on the presenter's screen must end up as the skipped demos left it.
void Classroom::resync_demos(Millis now) {
    demo_states_.clear();
    const auto here = player_.position(now);
    for (const auto& cue : player_.timeline().cues)
        if (cue.kind == CueKind::demo && cue.at < here) switch_demo(cue.detail.at("feature"), cue.detail.at("enabled"));
    ++demo_seq_;
}

void Classroom::run(Outbox& out, const Cue& cue, Millis now) {
    dirty_ = true;
    switch (cue.kind) {
        case CueKind::section_start: {
            // A seek can land here with the previous section's question still open. The previous
            // section's question, slide and line are over: late joiners must not be shown them.
            close_activity(out);
            activity_.reset();
            slide_ = nullptr; narration_ = nullptr;
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
        case CueKind::demo: {
            // Kept as state, not only announced: the presenter's tool polls and must be able to
            // catch up after a reconnect or a seek.
            switch_demo(cue.detail.at("feature"), cue.detail.at("enabled"));
            ++demo_seq_;
            out.push_back(to_all({{"type", "demo"}, {"feature", cue.detail.at("feature")}, {"enabled", cue.detail.at("enabled")}, {"note", cue.detail.at("note")},
                {"watch", cue.detail.at("watch")}}));
            return;
        }
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
        case CueKind::questions_share: share_questions(out, questions_.share_best(), false); return;
        case CueKind::section_end: return;
        case CueKind::lesson_end:
            tell_unanswered(out);
            out.push_back(to_all({{"type", "lesson.end"}}));
            return;
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
