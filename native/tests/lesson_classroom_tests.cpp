#include "lesson_fixture.hpp"
#include "classroom.hpp"
using namespace odeum;
using namespace odeum::lesson;

namespace {
struct Room {
    Classroom classroom;
    Millis now = 0;
    int tokens = 0;
    Room() : classroom(sample_plot(), std::shared_ptr<const LessonModel>(make_provisional_model()), [this] { return "token" + std::to_string(++tokens); }) {}
    std::string join(const std::string& name) {
        const auto admission = classroom.admit(name, "");
        classroom.connect(admission.student, now);
        return admission.student;
    }
    Outbox say(const std::string& student, Json message) { return classroom.from_student(student, message, now); }
    Outbox console(Json message) { return classroom.from_console(message, now); }
    Outbox advance(Millis by) { now += by; return classroom.tick(now); }
    Json student(const std::string& id) {
        const auto snapshot = classroom.console_snapshot(now);
        for (const auto& entry : snapshot.at("students")) if (entry.at("id") == id) return entry;
        throw std::runtime_error("No such student in snapshot");
    }
};
}

int main() { return run([] {
    Room room;
    const auto aoi = room.join("あおい"), haruto = room.join("はると"), riku = room.join("りく");
    check(aoi == "s1" && haruto == "s2" && riku == "s3", "Students are numbered as they arrive");

    // Before the lesson: nothing is open.
    rejects([&] { room.say(aoi, {{"type", "answer"}, {"activity_id", "missing-feel"}, {"text", "x"}}); }, "activity_closed");
    rejects([&] { room.say(aoi, {{"type", "progress"}, {"assignment_id", "restore-feel"}, {"step", 0}, {"done", true}}); }, "not_assigned");
    rejects([&] { room.say(aoi, {{"type", "dance"}}); }, "unknown_type");
    rejects([&] { room.say("s99", {{"type", "help"}}); }, "unknown_student");

    // The lesson starts: section, slide and the first line of narration reach everyone.
    room.console({{"type", "lesson.start"}});
    auto out = room.advance(0);
    check(find_type(out, "section") && find_type(out, "slide") && find_type(out, "narration"), "Opening cues are broadcast");
    check(find_type(out, "section")->audience == Audience::everyone && find_type(out, "section")->body.at("id") == "intro", "First section");
    check(find_type(out, "snapshot") && find_type(out, "snapshot")->audience == Audience::consoles, "Console gets a snapshot");
    out = room.advance(4 * 60'000);
    check(find_type(out, "demo") && find_type(out, "demo")->body.at("feature") == "all", "Demo switch is announced");

    // Operator opens the "what is missing" question by hand.
    out = room.console({{"type", "activity.open"}, {"id", "missing-feel"}});
    check(find_type(out, "activity.open")->body.at("activity").contains("code"), "Question with the stripped implementation");
    out = room.say(aoi, {{"type", "answer"}, {"activity_id", "missing-feel"}, {"text", "一瞬止まらないし、吹っ飛ばない"}});
    check(find_type(out, "answer.ack", aoi) && out.size() == 1, "Answers are acknowledged privately; nobody else sees them");
    // A spoken answer while a text question is open is taken as that student's answer.
    out = room.say(haruto, {{"type", "utterance"}, {"text", "カメラが揺れてない気がする"}});
    check(find_type(out, "answer.ack", haruto) && find_type(out, "heard")->body.at("intent") == "answer", "Speech is routed to the open question");
    check(room.student(aoi).at("answered") == true && room.student(riku).at("answered") == false, "Console sees who has answered");
    out = room.console({{"type", "activity.reveal"}});
    check(find_type(out, "activity.closed") && find_type(out, "activity.reveal") && count_type(out, "activity.personal") == 3, "Reveal: class result plus one personal result each");
    check(find_type(out, "activity.personal", aoi)->body.at("result").at("correct") == true, "Two points found");
    check(find_type(out, "activity.personal", haruto)->body.at("result").at("correct") == false, "One point is not enough");
    check(find_type(out, "activity.personal", riku)->body.at("result").at("answered") == false, "No answer is said gently");
    check(!find_type(out, "activity.reveal")->body.dump().empty() && find_type(out, "activity.reveal")->body.dump().find("あおい") == std::string::npos, "Class results carry no names");
    rejects([&] { room.say(riku, {{"type", "answer"}, {"activity_id", "missing-feel"}, {"text", "遅刻"}}); }, "activity_closed");

    // Questions: private answer for the asker, shared with the class on request, anonymously.
    out = room.say(aoi, {{"type", "question"}, {"text", "ヒットストップは何秒にするのがいいですか？なぜですか"}, {"show_name", false}});
    check(find_type(out, "reply", aoi) && find_type(out, "reply", aoi)->body.at("about") == "question" && !find_type(out, "question.shared"), "Asker is answered privately first");
    out = room.say(haruto, {{"type", "utterance"}, {"text", "ヒットストップは何秒にするのが良いですか？なぜ？"}});
    check(find_type(out, "reply", haruto)->body.at("text").get<std::string>().find("同じ質問") != std::string::npos, "Spoken duplicate becomes a me-too");
    out = room.console({{"type", "questions.share"}});
    const auto shared = find_type(out, "question.shared");
    check(shared && shared->audience == Audience::everyone && !shared->body.at("question").contains("name") && shared->body.at("question").at("votes") == 1, "Shared with everyone, without a name");
    check(find_type(out, "reply", aoi), "The asker is thanked");
    out = room.say(riku, {{"type", "question"}, {"text", "入力バッファはヒットストップ中にどう扱うのが理由として正しいですか"}});
    check(find_type(out, "attention") && find_type(out, "attention")->body.at("reason") == "question_needs_teacher", "Unanswerable questions go to the operator");

    // Assignment: progress, help, and individual hints.
    out = room.console({{"type", "assignment.assign"}, {"id", "restore-feel"}});
    check(find_type(out, "assignment")->body.at("assignment").at("steps").size() == 6, "Assignment is handed out");
    out = room.say(aoi, {{"type", "progress"}, {"assignment_id", "restore-feel"}, {"step", 0}, {"done", true}});
    check(find_type(out, "progress", aoi)->body.at("assignment").at("done")[0] == true, "Checklist is echoed back");
    out = room.say(haruto, {{"type", "utterance"}, {"text", "先生、動かないです。助けて"}});
    const auto hint = find_type(out, "hint", haruto);
    check(hint && hint->body.at("kind") == "hint" && hint->body.at("step") == 0 && !find_type(out, "hint", aoi), "A spoken request gets that student a hint, privately");
    out = room.advance(AssignmentBoard::start_grace_ms);
    check(find_type(out, "hint", riku) && find_type(out, "hint", riku)->body.at("kind") == "start_nudge", "The one who has not started is nudged");
    room.console({{"type", "progress.report"}, {"student", riku}, {"assignment_id", "restore-feel"}, {"step", 0}, {"done", true}});
    check(room.student(riku).at("progress").at("state") == "working", "Progress reported by a tool counts");

    // Voice: who is speaking shows on the console.
    room.say(aoi, {{"type", "voice.level"}, {"level", 70}});
    check(room.student(aoi).at("speaking") == true && room.classroom.console_snapshot(room.now).at("speakers") == std::vector<std::string>({aoi}), "Speaking student is visible");

    // Misbehaviour: warning, lockout, then removal; the rest of the class is untouched.
    out = room.say(riku, {{"type", "utterance"}, {"text", "wwwwwwwwww"}});
    check(find_type(out, "conduct", riku)->body.at("standing") == "warned" && find_type(out, "conduct.event"), "First disruption: warning, and the operator is told");
    room.say(riku, {{"type", "utterance"}, {"text", "ああああああああ"}});
    out = room.say(riku, {{"type", "utterance"}, {"text", "うんこ"}});
    const auto locked = find_type(out, "conduct", riku);
    check(locked && locked->body.at("standing") == "locked" && locked->body.at("remaining_ms") == Conduct::first_lock_ms && !locked->close, "Lockout with time remaining");
    out = room.say(riku, {{"type", "question"}, {"text", "なんで止められたんですか"}});
    check(out.size() == 1 && out[0].body.at("code") == "locked", "A locked student cannot post");
    room.say(riku, {{"type", "voice.level"}, {"level", 90}});
    check(room.student(riku).at("speaking") == false, "A locked student's microphone does not show as speaking");
    out = room.say(riku, {{"type", "progress"}, {"assignment_id", "restore-feel"}, {"step", 1}, {"done", true}});
    check(find_type(out, "progress", riku), "Locked students can still work on the assignment");
    out = room.advance(Conduct::first_lock_ms);
    check(find_type(out, "conduct", riku) && find_type(out, "conduct", riku)->body.at("standing") != "locked", "The lock lifts by itself");
    out = room.console({{"type", "student.eject"}, {"student", riku}});
    const auto ejected = find_type(out, "conduct", riku);
    check(ejected && ejected->body.at("standing") == "ejected" && ejected->close, "Removal closes the student's connection");
    rejects([&] { room.say(riku, {{"type", "help"}}); }, "ejected");
    rejects([&] { room.classroom.admit("りく", "token3"); }, "ejected");
    rejects([&] { room.classroom.admit("べつじん", ""); }, "entry_locked");
    check(room.classroom.admit("あおい", "token1").student == aoi, "Students already in class can reconnect");
    room.console({{"type", "entry.lock"}, {"locked", false}});
    check(room.classroom.admit("おくれてきた", "").student == "s4", "The operator can reopen the door");
    room.console({{"type", "student.readmit"}, {"student", riku}});
    check(room.classroom.admit("りく", "token3").student == riku, "Readmission restores the seat");

    // Operator tools.
    out = room.console({{"type", "student.lock"}, {"student", haruto}, {"seconds", 30}});
    check(find_type(out, "conduct", haruto)->body.at("remaining_ms") == 30'000, "Manual lock");
    out = room.console({{"type", "student.unlock"}, {"student", haruto}});
    check(find_type(out, "conduct", haruto)->body.at("standing") == "good", "Manual unlock");
    out = room.console({{"type", "student.tell"}, {"student", aoi}, {"text", "いい調子です"}});
    check(find_type(out, "reply", aoi)->body.at("about") == "teacher", "Private note from the operator");
    rejects([&] { room.console({{"type", "student.eject"}, {"student", "s99"}}); }, "unknown_student");
    rejects([&] { room.console({{"type", "activity.open"}, {"id", "nope"}}); }, "unknown_activity");
    rejects([&] { room.console({{"type", "explode"}}); }, "unknown_type");

    // Shared microphone: enrol, then diarized speech is attributed.
    out = room.say(aoi, {{"type", "voice.enroll"}});
    const auto code = find_type(out, "voice.enroll", aoi)->body.at("code").get<std::string>();
    out = room.console({{"type", "voice.diarized"}, {"speaker", "spk_2"}, {"text", code + "です"}});
    check(find_type(out, "voice.enrolled", aoi), "Number spoken -> label bound");
    out = room.console({{"type", "voice.diarized"}, {"speaker", "spk_2"}, {"text", "ノックバックの向きはどうして後ろ向きなんですか"}});
    check(find_type(out, "reply", aoi) && find_type(out, "heard")->body.at("student") == aoi, "Room-microphone speech reaches the right student");
    out = room.console({{"type", "voice.diarized"}, {"speaker", "spk_5"}, {"text", "だれでしょう"}});
    check(find_type(out, "heard")->body.at("intent") == "unattributed" && out.size() == 1, "Unknown voices are shown to the operator only");

    // A late joiner sees the current state: slide, open assignment, shared questions.
    const auto late = room.classroom.admit("まい", "");
    out = room.classroom.connect(late.student, room.now);
    const auto& state = find_type(out, "welcome", late.student)->body.at("state");
    check(state.contains("slide") && state.contains("assignment") && state.at("questions").size() == 1 && state.at("conduct").at("standing") == "good", "Welcome replays the lesson state");
    check(find_type(out, "welcome")->body.at("lesson").at("sections").size() == 9 && find_type(out, "welcome")->body.at("resume") == late.resume, "Welcome carries the outline and the resume token");

    // Run the whole timeline to the end: every scheduled question opens and closes.
    Room full;
    const auto only = full.join("ひとり");
    full.console({{"type", "lesson.start"}});
    std::size_t opened = 0, revealed = 0, assigned = 0, ended = 0, sections = 0;
    for (Millis t = 0; t <= 90 * 60'000; t += 1000) {
        const auto tick = full.advance(1000);
        opened += count_type(tick, "activity.open"); revealed += count_type(tick, "activity.reveal");
        assigned += count_type(tick, "assignment"); ended += count_type(tick, "lesson.end"); sections += count_type(tick, "section");
    }
    check(opened == 6 && revealed == 6 && assigned == 1 && ended == 1 && sections == 9, "A full run plays every part of the plot once");
    check(full.classroom.console_snapshot(full.now).at("player").at("state") == "finished", "Player finishes");
    check(full.student(only).at("standing") == "good", "A silent student is never penalised");

    // Capacity.
    Classroom tiny(sample_plot(), std::shared_ptr<const LessonModel>(make_provisional_model()), [] { return std::string("t"); }, 1);
    tiny.admit("a", "");
    rejects([&] { tiny.admit("b", ""); }, "capacity");
}); }
