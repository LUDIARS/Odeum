#include "lesson_fixture.hpp"
#include "classroom.hpp"
using namespace odeum;
using namespace odeum::lesson;

// What a student says to the teacher ("先生に話す", the microphone, the room microphone) is always
// acknowledged with how it was taken, and a report that the class could not be followed goes to
// the operator as needing attention, not to the question board.
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
    std::size_t board() { return classroom.console_snapshot(now).at("questions").size(); }
};
const Json* attention(const Outbox& out) {
    const auto found = find_type(out, "attention");
    return found ? &found->body : nullptr;
}
std::string taken_as(const Outbox& out, const std::string& student) {
    const auto ack = find_type(out, "utterance.ack", student);
    return ack ? ack->body.value("taken_as", std::string()) : std::string();
}
}

int main() { return run([] {
    // The sentence a student said in an attended class, after answering the open text question.
    const std::string could_not_see = "先生、すみません、デモの画面が小さくて world= の数字がどう縮んだのかよく見えませんでした。";
    Room room;
    const auto aoi = room.join("あおい"), haruto = room.join("はると");
    room.console({{"type", "lesson.start"}});
    room.classroom.tick(room.now);
    room.console({{"type", "activity.open"}, {"id", "hitstop-design"}});
    room.say(aoi, {{"type", "answer"}, {"activity_id", "hitstop-design"}, {"text", "止める長さと、何を止めるか"}});

    auto out = room.say(aoi, {{"type", "utterance"}, {"text", could_not_see}});
    check(taken_as(out, aoi) == "trouble", "Not seeing the demo is taken as trouble following the class");
    check(find_type(out, "utterance.ack", aoi)->body.at("note").get<std::string>().find("質問ボードには載せていません") != std::string::npos,
        "The student is told exactly how it was taken");
    check(attention(out) && attention(out)->at("reason") == "cannot_follow" && attention(out)->at("student") == aoi,
        "The operator is told it needs attention, with its own reason");
    check(find_type(out, "heard")->body.at("intent") == "trouble", "The console log shows the reading");
    check(!find_type(out, "reply") && room.board() == 0, "It does not enter the question board and gets no question reply");
    check(!find_type(out, "answer.ack") && room.classroom.connect(aoi, room.now)[0].body.at("state").at("activity").at("answer").at("text") == "止める長さと、何を止めるか",
        "The submitted answer is kept");

    // The same report typed into 質問する is not put on the board either.
    out = room.say(haruto, {{"type", "question"}, {"text", "スライドの文字が小さくてよく読めませんでした"}});
    check(attention(out) && attention(out)->at("reason") == "cannot_follow" && room.board() == 0, "A trouble report sent as a question goes to the operator");
    check(find_type(out, "reply", haruto) && find_type(out, "reply", haruto)->body.at("about") == "trouble", "…and the student is told so");

    // A real question that also mentions not seeing stays a question.
    out = room.say(haruto, {{"type", "question"}, {"text", "デモがよく見えなかったのですが、world= はどれくらい縮んだんですか？"}});
    check(!attention(out) || attention(out)->at("reason") != "cannot_follow", "A question mark keeps it a question");
    check(room.board() == 1, "…and it is on the board");

    // Every reading is acknowledged with what it was taken as.
    out = room.say(aoi, {{"type", "utterance"}, {"text", "なるほど"}});
    check(taken_as(out, aoi) == "remark" && find_type(out, "utterance.ack", aoi)->body.at("kept_answer") == "hitstop-design", "After answering: a remark");
    out = room.say(haruto, {{"type", "utterance"}, {"text", "威力で長さを変える"}});
    check(taken_as(out, haruto) == "answer" && find_type(out, "answer.ack", haruto), "Before answering: the answer");
    out = room.say(aoi, {{"type", "utterance"}, {"text", "ノックバックはどうして後ろ向きなんですか"}});
    check(taken_as(out, aoi) == "question" && find_type(out, "reply", aoi), "A question: on the board, answered privately, and acknowledged");
    room.console({{"type", "activity.reveal"}});
    out = room.say(aoi, {{"type", "utterance"}, {"text", "ヒットストップ楽しい"}});
    check(taken_as(out, aoi) == "chatter" && !find_type(out, "utterance.ack", aoi)->body.at("note").get<std::string>().empty(), "Chatter is acknowledged too");
    out = room.say(aoi, {{"type", "utterance"}, {"text", "教えてください、分からないです"}});
    check(taken_as(out, aoi) == "help" && attention(out) && attention(out)->at("reason") == "asked_for_help", "A call for help");
    out = room.say(haruto, {{"type", "utterance"}, {"text", "wwwwwwwwww"}});
    check(taken_as(out, haruto) == "disruption" && find_type(out, "conduct", haruto), "Noise is recorded and the student is told");
    out = room.say(haruto, {{"type", "utterance"}, {"text", "声が途切れてよく聞こえませんでした"}});
    check(taken_as(out, haruto) == "trouble" && attention(out)->at("reason") == "cannot_follow", "Not hearing the teacher is trouble too");
}); }
