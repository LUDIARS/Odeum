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
    // A refused tick is answered with the server's checklist (none yet), so the page can undo the box.
    auto refused = room.say(aoi, {{"type", "progress"}, {"assignment_id", "restore-feel"}, {"step", 0}, {"done", true}});
    check(refused.size() == 1 && refused[0].body.at("type") == "progress.rejected" && refused[0].body.at("code") == "not_assigned"
        && refused[0].body.at("assignment").is_null(), "Progress without an assignment is refused with the server state");
    rejects([&] { room.say(aoi, {{"type", "dance"}}); }, "unknown_type");
    rejects([&] { room.say("s99", {{"type", "help"}}); }, "unknown_student");

    // The lesson starts: section, slide and the first line of narration reach everyone.
    room.console({{"type", "lesson.start"}});
    auto out = room.advance(0);
    check(find_type(out, "section") && find_type(out, "slide") && find_type(out, "narration"), "Opening cues are broadcast");
    check(find_type(out, "section")->audience == Audience::everyone && find_type(out, "section")->body.at("id") == "intro", "First section");
    check(find_type(out, "snapshot") && find_type(out, "snapshot")->audience == Audience::consoles, "Console gets a snapshot");
    bool demoed = false;
    for (int i = 0; i < 6 * 60 && !demoed; ++i) { out = room.advance(1000); demoed = find_type(out, "demo") && find_type(out, "demo")->body.at("feature") == "all"; }
    check(demoed, "Demo switch is announced");

    // Operator opens the "what is missing" question by hand.
    out = room.console({{"type", "activity.open"}, {"id", "missing-feel"}});
    check(find_type(out, "activity.open")->body.at("activity").contains("code"), "Question with the stripped implementation");
    out = room.say(aoi, {{"type", "answer"}, {"activity_id", "missing-feel"}, {"text", "一瞬止まらないし、吹っ飛ばない"}});
    check(find_type(out, "answer.ack", aoi) && out.size() == 1, "Answers are acknowledged privately; nobody else sees them");
    // A spoken answer while a text question is open is taken as that student's answer.
    out = room.say(haruto, {{"type", "utterance"}, {"text", "カメラが揺れてない気がする"}});
    check(find_type(out, "answer.ack", haruto) && find_type(out, "heard")->body.at("intent") == "answer", "Speech is routed to the open question");
    const auto& spoken = find_type(out, "answer.ack", haruto)->body;
    check(spoken.at("via") == "utterance" && spoken.at("answer").at("text") == "カメラが揺れてない気がする", "The student is told the speech became their answer, with its text");
    // Said to the teacher after answering: a remark, never a replacement of the submitted answer.
    out = room.say(aoi, {{"type", "utterance"}, {"text", "デモが文字だけだと分かりにくかったです"}});
    check(!find_type(out, "answer.ack") && find_type(out, "utterance.ack", aoi) && find_type(out, "utterance.ack", aoi)->body.at("kept_answer") == "missing-feel"
        && find_type(out, "heard")->body.at("intent") == "remark", "An utterance does not overwrite a submitted answer");
    check(room.classroom.connect(aoi, room.now)[0].body.at("state").at("activity").at("answer").at("text") == "一瞬止まらないし、吹っ飛ばない", "The page is told the answer it holds");
    room.classroom.disconnect(aoi, room.now);
    check(room.student(aoi).at("answered") == true && room.student(riku).at("answered") == false, "Console sees who has answered");
    out = room.console({{"type", "activity.reveal"}});
    check(find_type(out, "activity.closed") && find_type(out, "activity.reveal") && count_type(out, "activity.personal") == 3, "Reveal: class result plus one personal result each");
    check(find_type(out, "activity.personal", aoi)->body.at("result").at("correct") == true, "Two points found");
    check(find_type(out, "activity.personal", haruto)->body.at("result").at("correct") == false, "One point is not enough");
    check(find_type(out, "activity.personal", riku)->body.at("result").at("answered") == false, "No answer is said gently");
    check(!find_type(out, "activity.reveal")->body.dump().empty() && find_type(out, "activity.reveal")->body.dump().find("あおい") == std::string::npos, "Class results carry no names");
    check(find_type(out, "activity.personal", aoi)->body.at("result").at("text") == "一瞬止まらないし、吹っ飛ばない"
        && find_type(out, "activity.reveal")->body.dump().find("分かりにくかった") == std::string::npos, "The remark is neither graded nor shown to the class");
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
    rejects([&] { room.say(aoi, {{"type", "question.vote"}, {"id", "q1"}}); }, "own_question");
    out = room.say(riku, {{"type", "question.vote"}, {"id", "q1"}});
    check(find_type(out, "question.voted", riku) && find_type(out, "question.votes")->audience == Audience::everyone
        && find_type(out, "question.votes")->body.at("votes") == 2, "A vote is confirmed and the class gets the server's count");
    out = room.say(riku, {{"type", "question"}, {"text", "入力バッファはヒットストップ中にどう扱うのが理由として正しいですか"}});
    check(find_type(out, "attention") && find_type(out, "attention")->body.at("reason") == "question_needs_teacher", "Unanswerable questions go to the operator");
    const auto pending = find_type(out, "attention")->body.at("question_id").get<std::string>();
    check(find_type(out, "reply", riku)->body.at("text").get<std::string>().find("人間の先生に回しました") != std::string::npos, "The asker is told who has the question");
    out = room.console({{"type", "questions.answer"}, {"id", pending}, {"text", "入力は覚えておき、止まり終わってから処理します"}});
    const auto teacher = find_type(out, "reply", riku);
    check(teacher && teacher->body.at("question_id") == pending && teacher->body.at("from") == "teacher"
        && teacher->body.at("text").get<std::string>().find("止まり終わってから") != std::string::npos, "The operator's answer reaches the asker as the answer to that question");
    rejects([&] { room.console({{"type", "questions.answer"}, {"id", "q999"}, {"text", "x"}}); }, "unknown_question");

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
    // During the assignment a "stuck" report sent as a question goes to the help path, not the board.
    room.advance(AssignmentBoard::help_cooldown_ms);
    const auto board_before = room.classroom.console_snapshot(room.now).at("questions").size();
    out = room.say(aoi, {{"type", "question"}, {"text", "手順2で進みません。hit_stop を呼んでいる行が見つからないです"}});
    check(find_type(out, "hint", aoi) && find_type(out, "reply", aoi)->body.at("about") == "help"
        && room.classroom.console_snapshot(room.now).at("questions").size() == board_before, "Stuck on the assignment: a hint, not a question for the board");

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
    // The room microphone follows the same rule: speech becomes an answer only while none is submitted.
    room.console({{"type", "activity.open"}, {"id", "hitstop-design"}});
    out = room.console({{"type", "voice.diarized"}, {"speaker", "spk_2"}, {"text", "威力に比例して長さを変える"}});
    check(find_type(out, "answer.ack", aoi) && find_type(out, "answer.ack", aoi)->body.at("via") == "utterance", "Diarized speech is taken as the answer, and the student is told");
    out = room.console({{"type", "voice.diarized"}, {"speaker", "spk_2"}, {"text", "さっきの実演が見えにくかった"}});
    check(!find_type(out, "answer.ack") && find_type(out, "utterance.ack", aoi), "Diarized speech never replaces a submitted answer");
    room.console({{"type", "activity.reveal"}});
    out = room.console({{"type", "voice.diarized"}, {"speaker", "spk_5"}, {"text", "だれでしょう"}});
    check(find_type(out, "heard")->body.at("intent") == "unattributed" && out.size() == 1, "Unknown voices are shown to the operator only");

    // A late joiner sees the current state: slide, open assignment, shared questions.
    const auto late = room.classroom.admit("まい", "");
    out = room.classroom.connect(late.student, room.now);
    const auto& state = find_type(out, "welcome", late.student)->body.at("state");
    check(state.contains("slide") && state.contains("assignment") && state.at("questions").size() == 1 && state.at("conduct").at("standing") == "good", "Welcome replays the lesson state");
    check(find_type(out, "welcome")->body.at("lesson").at("sections").size() == 9 && find_type(out, "welcome")->body.at("resume") == late.resume, "Welcome carries the outline and the resume token");

    // The assignment ends: a late tick is refused and answered with the server's checklist.
    room.console({{"type", "assignment.close"}});
    out = room.say(aoi, {{"type", "progress"}, {"assignment_id", "restore-feel"}, {"step", 3}, {"done", true}});
    const auto late_tick = find_type(out, "progress.rejected", aoi);
    check(late_tick && late_tick->body.at("code") == "not_assigned" && late_tick->body.at("assignment").at("done")[3] == false
        && late_tick->body.at("assignment").at("done")[0] == true, "The page gets the state to put the box back to");

    // A new section leaves the previous one's question, slide and line behind.
    Room moving;
    const auto mover = moving.join("うごく");
    moving.console({{"type", "lesson.start"}});
    moving.advance(0);
    moving.console({{"type", "lesson.skip"}});
    out = moving.advance(0);
    const auto demo = find_type(out, "demo");
    check(demo && demo->body.at("watch").get<std::string>().find("enemy_offset") != std::string::npos, "Demos say what to watch");
    moving.console({{"type", "activity.open"}, {"id", "missing-feel"}});
    moving.say(mover, {{"type", "answer"}, {"activity_id", "missing-feel"}, {"text", "止まらない"}});
    moving.console({{"type", "activity.reveal"}});
    moving.console({{"type", "lesson.skip"}});
    out = moving.advance(0);
    check(find_type(out, "section") && find_type(out, "section")->body.at("id") == "weapon", "Next section");
    const auto replay = moving.classroom.connect(mover, moving.now);
    const auto& now_state = replay[0].body.at("state");
    check(!now_state.contains("activity") && !now_state.contains("activity_results") && now_state.at("slide").at("section") == "weapon",
        "A joiner in the new section sees none of the previous section's results");

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
