#include "classroom.hpp"
#include "text.hpp"
#include <algorithm>

namespace odeum::lesson {
namespace {
const Json& field(const Json& message, const char* key) {
    const auto found = message.find(key);
    if (found == message.end()) throw ProtocolError("invalid_message", "Missing field");
    return *found;
}
std::string text_field(const Json& message, const char* key, std::size_t maximum) {
    const auto& value = field(message, key);
    if (!value.is_string()) throw ProtocolError("invalid_message", "Text field required");
    auto text = trim(value.get_ref<const std::string&>());
    const auto length = utf8_length(text);
    if (length == 0 || length > maximum) throw ProtocolError("invalid_message", "Text length out of range");
    return text;
}
std::int64_t integer_field(const Json& message, const char* key, std::int64_t low, std::int64_t high) {
    const auto& value = field(message, key);
    if (!value.is_number_integer()) throw ProtocolError("invalid_message", "Integer field required");
    const auto number = value.get<std::int64_t>();
    if (number < low || number > high) throw ProtocolError("invalid_message", "Integer out of range");
    return number;
}
bool flag_field(const Json& message, const char* key) {
    const auto& value = field(message, key);
    if (!value.is_boolean()) throw ProtocolError("invalid_message", "Boolean field required");
    return value.get<bool>();
}
const char* intent_name(Intent intent) {
    switch (intent) {
        case Intent::question: return "question";
        case Intent::answer: return "answer";
        case Intent::help: return "help";
        case Intent::disruption: return "disruption";
        case Intent::chatter: break;
    }
    return "chatter";
}
}

Classroom::Classroom(Plot plot, std::shared_ptr<const LessonModel> model, TokenSource tokens, std::size_t max_students)
    : plot_(std::move(plot)), model_(std::move(model)), tokens_(std::move(tokens)), max_students_(max_students),
      player_(build_timeline(plot_, *model_)), questions_(*model_) {}

Context Classroom::context() const {
    return {&plot_, section_, activity_ && activity_->state() == Activity::State::open && activity_->takes_text(), assignment_active_};
}
Classroom::Student& Classroom::student(const std::string& id) {
    const auto found = students_.find(id);
    if (found == students_.end()) throw ProtocolError("unknown_student", "Unknown student");
    return found->second;
}

Classroom::Admission Classroom::admit(const std::string& name, std::string_view resume) {
    if (const auto known = resumes_.find(resume); known != resumes_.end()) {
        if (conduct_.ejected(known->second)) throw ProtocolError("ejected", "Removed from this class");
        return {known->second, std::string(resume)};
    }
    if (entry_locked_) throw ProtocolError("entry_locked", "This class is not taking new students");
    if (students_.size() >= max_students_) throw ProtocolError("capacity", "Class is full");
    Student entry{"s" + std::to_string(++next_student_), name, tokens_(), 0};
    const Admission admission{entry.id, entry.resume};
    resumes_.emplace(entry.resume, entry.id);
    students_.emplace(entry.id, std::move(entry));
    return admission;
}
Outbox Classroom::connect(const std::string& id, Millis now) {
    auto& who = student(id);
    if (conduct_.ejected(id)) throw ProtocolError("ejected", "Removed from this class");
    ++who.connections;
    if (assignment_ && assignment_active_) assignment_->enroll(id, now);
    dirty_ = true;
    return {to_student(id, welcome(who, now))};
}
Outbox Classroom::disconnect(const std::string& id, Millis) {
    const auto found = students_.find(id);
    if (found == students_.end() || found->second.connections == 0) return {};
    if (--found->second.connections == 0) voice_.leave(id);
    dirty_ = true;
    return {};
}

bool Classroom::allowed_to_post(Outbox& out, const Student& from, Millis now) {
    if (!conduct_.may_post(from.id, now)) {
        out.push_back(to_student(from.id, error_message("locked", "You cannot post right now")));
        return false;
    }
    const auto verdict = conduct_.post(from.id, now);
    apply(out, from.id, verdict, now);
    return verdict.action == Conduct::Action::none || verdict.action == Conduct::Action::warn;
}
void Classroom::apply(Outbox& out, const std::string& id, const Conduct::Verdict& verdict, Millis now) {
    if (verdict.action == Conduct::Action::none) return;
    dirty_ = true;
    auto notice = standing_json(id, now);
    notice["reason"] = verdict.reason;
    out.push_back(to_consoles({{"type", "conduct.event"}, {"student", id}, {"standing", notice.at("standing")}, {"reason", verdict.reason}}));
    if (verdict.action != Conduct::Action::eject) { out.push_back(to_student(id, std::move(notice))); return; }
    // Removal takes the student's pending work out of the class and closes the door behind them,
    // so the same person cannot simply walk back in under a new name.
    if (assignment_) assignment_->withdraw(id);
    questions_.withdraw(id);
    voice_.leave(id);
    speakers_.forget(id);
    entry_locked_ = true;
    auto last = to_student(id, std::move(notice));
    last.close = true;
    out.push_back(std::move(last));
}

void Classroom::answer(Outbox& out, const Student& from, const Json& message, Millis) {
    if (!activity_ || field(message, "activity_id") != activity_->plan().id) throw ProtocolError("activity_closed", "No such open question");
    activity_->answer(from.id, message);
    dirty_ = true;
    out.push_back(to_student(from.id, {{"type", "answer.ack"}, {"activity_id", activity_->plan().id}, {"via", "form"},
        {"answer", activity_->submitted(from.id)}}));
}
void Classroom::ask(Outbox& out, const Student& from, const std::string& text, bool show_name, Millis now) {
    const auto submitted = questions_.submit(from.id, from.name, show_name, text, context(), now);
    dirty_ = true;
    // The asker always gets an answer of their own, whether or not the class hears the question.
    const auto prefix = !submitted.duplicate ? std::string() : submitted.own ? "この質問はすでに受け取っています。"
        : "同じ質問がすでに出ています。あなたの分も「知りたい」に加えました。";
    out.push_back(to_student(from.id, {{"type", "reply"}, {"about", "question"}, {"question_id", submitted.id},
        {"relation", submitted.duplicate && !submitted.own ? "me_too" : "asker"}, {"text", prefix + submitted.review.answer},
        {"pending_teacher", submitted.review.needs_teacher}}));
    if (submitted.review.needs_teacher && !submitted.duplicate)
        out.push_back(to_consoles({{"type", "attention"}, {"student", from.id}, {"reason", "question_needs_teacher"}, {"question_id", submitted.id}}));
}
// A message sent as a question that reads as "I am stuck on the assignment" goes where it can be
// helped: the hint for the step the student is on, not the question board.
void Classroom::report_stuck(Outbox& out, const Student& from, const std::string& text, Millis now) {
    out.push_back(to_consoles({{"type", "heard"}, {"student", from.id}, {"intent", "help"}, {"text", text}}));
    const auto before = out.size();
    ask_for_help(out, from, now);
    const bool hinted = std::any_of(out.begin() + static_cast<std::ptrdiff_t>(before), out.end(), [&](const Outbound& message) {
        return message.body.value("type", std::string()) == "hint" && (message.audience == Audience::everyone || message.student == from.id); });
    out.push_back(to_student(from.id, {{"type", "reply"}, {"about", "help"},
        {"text", hinted ? "質問ではなく、課題で手が止まっているという知らせとして受け取りました。いまの手順のヒントを送りました。先生の画面にも出ています。"
                        : "質問ではなく、課題で手が止まっているという知らせとして受け取りました。先生の画面に出ています。"}}));
}
void Classroom::ask_for_help(Outbox& out, const Student& from, Millis now) {
    dirty_ = true;
    if (assignment_ && assignment_active_) {
        assignment_->enroll(from.id, now);
        assignment_->request_help(from.id, now);
        supervise(out, now);  // answer now rather than on the next tick
        return;
    }
    out.push_back(to_student(from.id, {{"type", "reply"}, {"about", "help"},
        {"text", section_ ? "「" + section_->title + "」のどこで引っかかったかを一言で教えてください。先生にも伝えました。" : "授業が始まったら、分からないところをもう一度教えてください。"}}));
    out.push_back(to_consoles({{"type", "attention"}, {"student", from.id}, {"reason", "asked_for_help"}}));
}
void Classroom::hear(Outbox& out, const Student& from, const std::string& text, Millis now) {
    const auto reading = model_->read(text, context());
    // Something said to the teacher never replaces an answer the student already submitted: it is a
    // remark, kept for the operator. Before the student has answered, it is taken as the answer and the
    // student is told so, with the text, so the page can show it as their answer.
    const bool remark = reading.intent == Intent::answer && activity_->answered(from.id);
    // The operator sees what was said and how it was read, so a wrong reading can be corrected by hand.
    out.push_back(to_consoles({{"type", "heard"}, {"student", from.id}, {"intent", remark ? "remark" : intent_name(reading.intent)}, {"text", text}}));
    switch (reading.intent) {
        case Intent::disruption: apply(out, from.id, conduct_.disruption(from.id, reading.severity, now), now); return;
        case Intent::help: ask_for_help(out, from, now); return;
        case Intent::question: ask(out, from, text, false, now); return;
        case Intent::answer:
            if (remark) {
                if (!reading.on_topic) apply(out, from.id, conduct_.off_topic(from.id, now), now);
                out.push_back(to_student(from.id, {{"type", "utterance.ack"}, {"kept_answer", activity_->plan().id},
                    {"note", "先生に伝えました。問いへの回答はすでに受け取っているので、いまの言葉で置き換えていません。"}}));
                return;
            }
            activity_->answer(from.id, {{"text", text}});
            dirty_ = true;
            out.push_back(to_student(from.id, {{"type", "answer.ack"}, {"activity_id", activity_->plan().id}, {"via", "utterance"},
                {"answer", activity_->submitted(from.id)}}));
            return;
        case Intent::chatter:
            if (!reading.on_topic) apply(out, from.id, conduct_.off_topic(from.id, now), now);
            out.push_back(to_student(from.id, {{"type", "utterance.ack"}}));
            return;
    }
}

// One step of the assignment changed, whoever saw it: the student's own tick, a tool watching
// the student's game, or the operator. The student's page is told in every case.
void Classroom::report_progress(Outbox& out, const std::string& id, const Json& message, Millis now) {
    if (!assignment_ || !assignment_active_ || field(message, "assignment_id") != assignment_->plan().id)
        throw ProtocolError("not_assigned", "No such assignment");
    assignment_->enroll(id, now);
    assignment_->report(id, static_cast<std::size_t>(integer_field(message, "step", 0, 1000)), flag_field(message, "done"), now);
    dirty_ = true;
    out.push_back(to_student(id, {{"type", "progress"}, {"assignment", assignment_->progress(id)}}));
    supervise(out, now);
}
Outbox Classroom::from_tool(const std::string& id, const Json& message, Millis now) {
    Outbox out;
    student(id);
    if (conduct_.ejected(id)) throw ProtocolError("ejected", "Removed from this class");
    report_progress(out, id, message, now);
    return out;
}

Outbox Classroom::from_student(const std::string& id, const Json& message, Millis now) {
    Outbox out;
    const auto& from = student(id);
    if (conduct_.ejected(id)) throw ProtocolError("ejected", "Removed from this class");
    const auto type = text_field(message, "type", 32);
    if (type == "voice.level") {
        if (!conduct_.may_post(id, now)) return out;  // a locked student's microphone is not shown as speaking
        if (voice_.level(id, static_cast<int>(integer_field(message, "level", 0, 100)), now)) dirty_ = true;
        return out;
    }
    if (type == "progress") {
        // A refused tick is answered with the server's own checklist, so the page can put the box back.
        try { report_progress(out, id, message, now); }
        catch (const ProtocolError& refused) {
            Json reply{{"type", "progress.rejected"}, {"code", refused.code}, {"message", refused.what()}, {"assignment", nullptr}};
            if (assignment_) reply["assignment"] = assignment_->progress(id);
            out.push_back(to_student(id, std::move(reply)));
        }
        return out;
    }
    // Working on the assignment and asking for a hint stay available during a lockout:
    // the lock silences a student in front of the class, it does not stop them learning.
    if (type == "help") { ask_for_help(out, from, now); return out; }
    if (type == "voice.enroll") {
        out.push_back(to_student(id, {{"type", "voice.enroll"}, {"code", speakers_.begin_enrollment(id, now)},
            {"seconds", SpeakerRegistry::enrollment_ms / 1000}}));
        return out;
    }
    if (type != "answer" && type != "question" && type != "question.vote" && type != "utterance")
        throw ProtocolError("unknown_type", "Unknown message type");
    if (!allowed_to_post(out, from, now)) return out;
    if (type == "answer") answer(out, from, message, now);
    else if (type == "question") {
        const auto text = text_field(message, "text", QuestionBoard::max_scalars);
        if (assignment_ && assignment_active_ && model_->read(text, context()).intent == Intent::help) report_stuck(out, from, text, now);
        else ask(out, from, text, message.value("show_name", false), now);
    }
    else if (type == "question.vote") {
        const auto question = text_field(message, "id", 32);
        const auto votes = questions_.vote(id, question);
        dirty_ = true;
        out.push_back(to_student(id, {{"type", "question.voted"}, {"id", question}}));
        // Counts on the page come only from here, so nobody sees a number the server did not keep.
        if (questions_.shared(question)) out.push_back(to_all({{"type", "question.votes"}, {"id", question}, {"votes", votes}}));
    }
    else hear(out, from, text_field(message, "text", max_utterance_scalars), now);
    return out;
}

Outbox Classroom::from_console(const Json& message, Millis now) {
    Outbox out;
    const auto type = text_field(message, "type", 32);
    const auto target = [&] { return student(text_field(message, "student", 32)).id; };
    dirty_ = true;
    if (type == "lesson.start") player_.start(now);
    else if (type == "lesson.pause") player_.pause(now);
    else if (type == "lesson.resume") player_.resume(now);
    else if (type == "lesson.skip") { if (!player_.skip_section(now)) throw ProtocolError("invalid_state", "No later section"); resync_demos(now); }
    else if (type == "lesson.seek") { player_.seek(integer_field(message, "position_ms", 0, player_.timeline().duration), now); resync_demos(now); }
    else if (type == "activity.open") open_activity(out, text_field(message, "id", 64));
    else if (type == "activity.close") close_activity(out);
    else if (type == "activity.reveal") reveal_activity(out);
    else if (type == "assignment.assign") assign(out, text_field(message, "id", 64), now);
    else if (type == "assignment.close") close_assignment(out);
    else if (type == "questions.share") {
        if (message.contains("id")) { const auto id = text_field(message, "id", 32); questions_.share(id); share_questions(out, {id}); }
        else share_questions(out, questions_.share_best());
    }
    else if (type == "questions.dismiss") questions_.dismiss(text_field(message, "id", 32));
    else if (type == "questions.answer") answer_question(out, text_field(message, "id", 32), text_field(message, "text", max_tell_scalars));
    else if (type == "student.lock") {
        const auto id = target();
        apply(out, id, conduct_.lock(id, integer_field(message, "seconds", 1, 900) * 1000, now, "先生の判断で一時的に発言を止めています"), now);
    }
    else if (type == "student.unlock") { const auto id = target(); conduct_.unlock(id); out.push_back(to_student(id, standing_json(id, now))); }
    else if (type == "student.eject") { const auto id = target(); apply(out, id, conduct_.eject(id, "先生の判断で退出になりました"), now); }
    else if (type == "student.readmit") conduct_.readmit(target());
    else if (type == "student.tell") {
        const auto id = target();
        out.push_back(to_student(id, {{"type", "reply"}, {"about", "teacher"}, {"text", text_field(message, "text", max_tell_scalars)}}));
    }
    else if (type == "entry.lock") entry_locked_ = flag_field(message, "locked");
    else if (type == "voice.diarized") {
        // One shared microphone: the diarizer's label decides whose turn this is.
        const auto text = text_field(message, "text", max_utterance_scalars);
        const auto heard = speakers_.heard(text_field(message, "speaker", 64), text, now);
        if (!heard.student) out.push_back(to_consoles({{"type", "heard"}, {"student", nullptr}, {"intent", "unattributed"}, {"text", text}}));
        else if (heard.enrolled_now) out.push_back(to_student(*heard.student, {{"type", "voice.enrolled"}}));
        else if (const auto& from = student(*heard.student); allowed_to_post(out, from, now)) hear(out, from, text, now);
    }
    else if (type == "progress.report") report_progress(out, target(), message, now);  // the operator ticks a step for a student
    else throw ProtocolError("unknown_type", "Unknown console command");
    return out;
}
}
