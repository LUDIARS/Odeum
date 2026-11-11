#include "lesson_fixture.hpp"
#include "classroom.hpp"
using namespace odeum;
using namespace odeum::lesson;

// A student's own questions: each has a state the student can see (答えあり / 先生の回答待ち /
// 先生から回答 / 見送り), the asker can say a prepared answer did not settle it, sharing says only what
// the rules know, and nobody leaves the lesson still waiting without being told.
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
    Json mine(const std::string& student) { return classroom.connect(student, now)[0].body.at("state").at("my_questions"); }
};
const Json& status(const Outbox& out, const std::string& student) {
    const auto found = find_type(out, "question.status", student);
    if (!found) throw std::runtime_error("No question.status for " + student);
    return found->body.at("question");
}
std::string reply_text(const Outbox& out, const std::string& student) {
    const auto found = find_type(out, "reply", student);
    return found ? found->body.at("text").get<std::string>() : std::string();
}
}

int main() { return run([] {
    Room room;
    const auto aoi = room.join("あおい"), haruto = room.join("はると"), riku = room.join("りく");
    room.console({{"type", "lesson.start"}});
    room.classroom.tick(room.now);
    // Prepared answers on damage and power, and on hit stop length, wait for these activities (lesson_faq_tests).
    for (const auto* id : {"hitstop-design", "knockback-effects"}) { room.console({{"type", "activity.open"}, {"id", id}}); room.console({{"type", "activity.reveal"}}); }

    // A question with a prepared answer: 答えあり, with the answer.
    auto out = room.say(aoi, {{"type", "question"}, {"text", "ダメージと威力を分けておくと、具体的にどんなときに便利なんですか？"}});
    const auto power = status(out, aoi).at("id").get<std::string>();
    check(status(out, aoi).at("state") == "answered" && !status(out, aoi).at("answer").get<std::string>().empty() && status(out, aoi).at("relation") == "asker",
        "A prepared answer shows as 答えあり in the asker's list");
    out = room.say(haruto, {{"type", "question"}, {"text", "ダメージと威力を分けておくと、具体的にどんなときに便利なんですか"}});
    check(status(out, haruto).at("relation") == "me_too" && status(out, haruto).at("id") == power, "A me-too sees the question in their list too");

    // Without an answer: 先生の回答待ち, then the teacher's answer reaches both lists.
    out = room.say(riku, {{"type", "question"}, {"text", "ノックバックで次の攻撃が届かなくなる問題は、実際のゲームではどう解決していますか"}});
    const auto combo = status(out, riku).at("id").get<std::string>();
    check(status(out, riku).at("state") == "waiting" && status(out, riku).at("answer") == "", "No prepared answer: 先生の回答待ち");
    check(room.classroom.console_snapshot(room.now).at("questions_waiting") == 1, "The console counts questions waiting for the teacher");
    out = room.console({{"type", "questions.answer"}, {"id", combo}, {"text", "押す強さを弱めにする作品がよくあります"}});
    check(status(out, riku).at("state") == "teacher" && status(out, riku).at("answer") == "押す強さを弱めにする作品がよくあります", "先生から回答");
    check(room.classroom.console_snapshot(room.now).at("questions_waiting") == 0, "Answered: no longer waiting");

    // 「まだ分からない」: back to waiting, out of the automatic share, the operator is told.
    rejects([&] { room.say(haruto, {{"type", "question.unresolved"}, {"id", power}}); }, "not_your_question");
    out = room.say(aoi, {{"type", "question.unresolved"}, {"id", power}});
    check(status(out, aoi).at("state") == "waiting" && status(out, aoi).at("unresolved") == true && status(out, haruto).at("state") == "waiting",
        "The asker's 「まだ分からない」 returns it to 先生の回答待ち for everyone who asked");
    check(find_type(out, "attention") && find_type(out, "attention")->body.at("reason") == "question_unresolved", "The operator is asked to answer it");
    check(reply_text(out, aoi).find("自動ではクラスに共有しません") != std::string::npos, "The asker is told what happens now");
    rejects([&] { room.say(aoi, {{"type", "question.unresolved"}, {"id", power}}); }, "no_answer_yet");
    out = room.console({{"type", "questions.share"}});
    check(!find_type(out, "question.shared"), "A question its asker marked unresolved is not shared automatically");

    // Sharing tells the asker why, in terms the rules can back, and never praises.
    out = room.say(haruto, {{"type", "question"}, {"text", "ヒットストップは何秒にするのがいいですか？なぜですか"}});
    const auto seconds = status(out, haruto).at("id").get<std::string>();
    room.say(riku, {{"type", "question.vote"}, {"id", seconds}});
    out = room.console({{"type", "questions.share"}});
    check(find_type(out, "question.shared") && find_type(out, "question.shared")->body.at("question").at("answer_from") == "prepared", "Shared with where its answer came from");
    const auto thanks = reply_text(out, haruto);
    check(thanks.find("いい質問") == std::string::npos && thanks.find("規則による評価") != std::string::npos && thanks.find("1 人") != std::string::npos
        && thanks.find("分かりません") != std::string::npos, "The sharing note says what is true: prepared answer, rule score, me-too count");
    check(status(out, haruto).at("shared") == true, "The asker's list shows it as shared");

    // Dismissal tells the asker and the me-toos plainly.
    out = room.say(riku, {{"type", "question"}, {"text", "今日の給食は何ですか"}});
    const auto lunch = status(out, riku).at("id").get<std::string>();
    out = room.console({{"type", "questions.dismiss"}, {"id", lunch}});
    check(status(out, riku).at("state") == "dismissed" && reply_text(out, riku).find("見送り") != std::string::npos
        && reply_text(out, riku).find("届きません") != std::string::npos, "A dismissed question says so to its asker");

    // The lesson ends with a question still waiting: its asker and me-too are told, once.
    room.console({{"type", "lesson.seek"}, {"position_ms", 90 * 60'000 - 1000}});
    out = room.classroom.tick(room.now += 2000);
    check(find_type(out, "lesson.end"), "The lesson ends");
    const auto told = find_type(out, "reply", aoi);
    check(told && told->body.at("about") == "questions_unanswered" && told->body.at("count") == 1
        && told->body.at("text").get<std::string>().find("答えられませんでした") != std::string::npos, "The asker is told it was not answered in class");
    check(find_type(out, "reply", haruto) && find_type(out, "reply", haruto)->body.at("about") == "questions_unanswered", "So is the me-too");
    check(!find_type(out, "reply", riku), "Nothing for a student whose questions were answered or dismissed");
    check(room.mine(aoi).size() == 1 && room.mine(aoi)[0].at("state") == "waiting", "The welcome carries the student's own list");
    room.console({{"type", "lesson.seek"}, {"position_ms", 90 * 60'000 - 1000}});
    room.console({{"type", "lesson.resume"}});
    out = room.classroom.tick(room.now += 2000);
    check(!find_type(out, "reply", aoi), "Told once only");
}); }
