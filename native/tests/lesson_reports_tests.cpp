#include "lesson_fixture.hpp"
#include "classroom.hpp"
using namespace odeum;
using namespace odeum::lesson;

// A can't-follow report ("the demo was too small to read") gets a follow-up: it stays on the console
// until the operator acknowledges or answers it, the reporter's page shows where it stands, an answer
// reaches the reporter as a message from the teacher, and reports nobody acknowledged are named at the end.
namespace {
struct Room {
    Classroom classroom;
    Millis now = 0;
    int tokens = 0;
    Room() : classroom(sample_plot(), std::shared_ptr<const LessonModel>(make_provisional_model()), [this] { return "token" + std::to_string(++tokens); }) {}
    std::string join(const std::string& name) { const auto admission = classroom.admit(name, ""); classroom.connect(admission.student, now); return admission.student; }
    Outbox say(const std::string& student, Json message) { return classroom.from_student(student, message, now); }
    Outbox console(Json message) { return classroom.from_console(message, now); }
    Json snapshot() { return classroom.console_snapshot(now); }
    Json welcome(const std::string& student) { return classroom.connect(student, now)[0].body.at("state"); }
};
const Json& report(const Outbox& out, const std::string& student) {
    const auto found = find_type(out, "report.status", student);
    if (!found) throw std::runtime_error("No report.status for " + student);
    return found->body.at("report");
}
const char* const not_seen = "先生、すみません、デモの画面が小さくて world= の数字がよく見えませんでした。";
const char* const not_heard = "先生の声が途切れて聞こえなかったです";
}

int main() { return run([] {
    Room room;
    const auto aoi = room.join("あおい"), haruto = room.join("はると");
    room.console({{"type", "lesson.start"}});
    room.classroom.tick(room.now);

    // Said to the teacher: a report the student can follow.
    auto out = room.say(aoi, {{"type", "utterance"}, {"text", not_seen}});
    const auto& first = report(out, aoi);
    const auto seen_id = first.at("id").get<std::string>();
    check(first.at("state") == "reported" && first.at("text") == not_seen, "The reporter's page gets the report as 知らせました");
    check(find_type(out, "attention")->body.at("report_id") == seen_id, "The console's notice names the report");
    check(find_type(out, "utterance.ack", aoi)->body.at("note").get<std::string>().find("あなたの知らせ") != std::string::npos, "The student is told where to look");
    auto snapshot = room.snapshot();
    check(snapshot.at("reports_open") == 1 && snapshot.at("reports").size() == 1 && snapshot.at("reports")[0].at("student") == aoi
        && snapshot.at("reports")[0].at("text") == not_seen, "The console keeps it, with the student and the text");
    check(room.welcome(aoi).at("my_reports").size() == 1 && room.welcome(haruto).at("my_reports").empty(), "A page that reconnects gets its own reports only");

    // Sent from 「質問する」: the same report.
    out = room.say(haruto, {{"type", "question"}, {"text", not_heard}});
    const auto heard_id = report(out, haruto).at("id").get<std::string>();
    check(room.snapshot().at("reports_open") == 2, "Both stay open on the console");

    // Acknowledged: off the open count, the reporter sees 先生が確認しました.
    out = room.console({{"type", "reports.ack"}, {"id", seen_id}});
    check(report(out, aoi).at("state") == "acknowledged" && out.size() == 1, "Only the reporter is told it was acknowledged");
    check(room.snapshot().at("reports_open") == 1, "An acknowledged report is no longer open");

    // Answered: a message from the teacher to that student, and 先生から返事.
    out = room.console({{"type", "reports.reply"}, {"id", heard_id}, {"text", "マイクを替えました。聞こえにくければもう一度知らせてください"}});
    const auto reply = find_type(out, "reply", haruto);
    check(reply && reply->body.at("about") == "report" && reply->body.at("from") == "teacher" && reply->body.at("report_id") == heard_id
        && reply->body.at("text").get<std::string>().find("マイクを替えました") != std::string::npos, "The answer reaches the reporter as the teacher's");
    check(report(out, haruto).at("state") == "replied" && report(out, haruto).at("reply") == "マイクを替えました。聞こえにくければもう一度知らせてください"
        && !find_type(out, "reply", aoi), "…with the state, and nobody else hears it");
    check(room.snapshot().at("reports_open") == 0, "Nothing is open");
    room.console({{"type", "reports.ack"}, {"id", heard_id}});
    check(room.snapshot().at("reports")[1].at("state") == "replied", "Acknowledging after a reply keeps it replied");
    rejects([&] { room.console({{"type", "reports.ack"}, {"id", "r99"}}); }, "unknown_report");

    // At the end: a report nobody acknowledged is named to its reporter, once; acknowledged ones are not.
    room.say(aoi, {{"type", "utterance"}, {"text", "スライドの文字が小さくて読めなかったです"}});
    out = room.console({{"type", "lesson.seek"}, {"position_ms", room.classroom.console_welcome(room.now).at("timeline").at("duration_ms").get<Millis>()}});
    out = room.classroom.tick(room.now);
    std::string told;
    for (const auto& message : out) if (message.student == aoi && message.body.value("about", std::string()) == "reports_unacknowledged") told = message.body.at("text");
    check(told.find("スライドの文字が小さくて読めなかった") != std::string::npos && told.find("world=") == std::string::npos, "Only the unacknowledged report is named at the end");
    for (const auto& message : out) check(message.body.value("about", std::string()) != "reports_unacknowledged" || message.student == aoi, "Nobody else is told");

    // An ejected student's reports leave the console.
    room.console({{"type", "student.eject"}, {"student", aoi}});
    check(room.snapshot().at("reports").size() == 1, "Ejection takes the student's reports off the console");
}); }
