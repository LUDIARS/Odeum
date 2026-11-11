#include "lesson_fixture.hpp"
#include "classroom.hpp"
#include "text.hpp"
#include <algorithm>
using namespace odeum;
using namespace odeum::lesson;

// A prepared answer must not give away an in-class question before that question has been revealed.
// An FAQ entry that would names the activity in `after`; until that activity's results are shown the
// entry is not used and the question goes to the human teacher.
namespace {
// The longest run of scalars two folded texts share that holds at least `min_wide` non-ASCII scalars
// (ASCII names such as "actiongamesample" appear everywhere and say nothing about the answer).
std::size_t shared_run(const std::u32string& a, const std::u32string& b, std::size_t min_wide) {
    std::vector<std::size_t> previous(b.size() + 1, 0), current(b.size() + 1, 0);
    std::size_t best = 0;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        for (std::size_t j = 1; j <= b.size(); ++j) {
            current[j] = a[i - 1] == b[j - 1] ? previous[j - 1] + 1 : 0;
            if (current[j] <= best) continue;
            const auto run = std::u32string_view(a).substr(i - current[j], current[j]);
            if (static_cast<std::size_t>(std::count_if(run.begin(), run.end(), [](char32_t c) { return c > 0x7f; })) >= min_wide) best = current[j];
        }
        std::swap(previous, current);
    }
    return best;
}
constexpr std::size_t spoiler_run = 10, spoiler_wide = 5;

// Does this prepared answer give away the activity? A text / missing answer the rules would grade
// correct, or a passage shared with the activity's own explanation or model answer.
bool gives_away(const LessonModel& model, const Faq& faq, const ActivityPlan& activity) {
    if ((activity.kind == ActivityKind::text || activity.kind == ActivityKind::missing) && model.grade(activity, faq.answer).correct) return true;
    const auto answer = fold(faq.answer);
    for (const auto* reveal : {&activity.explanation, &activity.model_answer})
        if (!reveal->empty() && shared_run(answer, fold(*reveal), spoiler_wide) >= spoiler_run) return true;
    return false;
}

struct Room {
    Classroom classroom;
    Millis now = 0;
    int tokens = 0;
    Room() : classroom(sample_plot(), std::shared_ptr<const LessonModel>(make_provisional_model()), [this] { return "token" + std::to_string(++tokens); }) {}
    std::string join(const std::string& name) { const auto admission = classroom.admit(name, ""); classroom.connect(admission.student, now); return admission.student; }
    Outbox say(const std::string& student, Json message) { return classroom.from_student(student, message, now); }
    Outbox console(Json message) { return classroom.from_console(message, now); }
};
const Json& status(const Outbox& out, const std::string& student) {
    const auto found = find_type(out, "question.status", student);
    if (!found) throw std::runtime_error("No question.status");
    return found->body.at("question");
}
}

int main() { return run([] {
    const auto plot = sample_plot();
    const auto model = make_provisional_model();

    // Content guard for the sample course: every FAQ answer that gives an activity away waits for it.
    std::vector<const ActivityPlan*> order;
    for (const auto& section : plot.sections) if (section.activity) order.push_back(&*section.activity);
    const auto position = [&](std::string_view id) {
        for (std::size_t i = 0; i < order.size(); ++i) if (order[i]->id == id) return static_cast<std::ptrdiff_t>(i);
        return static_cast<std::ptrdiff_t>(-1);
    };
    std::string problems;
    std::size_t gated = 0;
    for (const auto& faq : plot.faq) {
        gated += !faq.after.empty();
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (!gives_away(*model, faq, *order[i])) continue;
            if (faq.after.empty() || position(faq.after) < static_cast<std::ptrdiff_t>(i))
                problems += "\n  FAQ [" + faq.keywords.front() + " / " + faq.keywords.back() + "] gives away " + order[i]->id + " but has after=\"" + faq.after + "\"";
        }
    }
    if (!problems.empty()) throw std::runtime_error("Prepared answers give away later questions:" + problems);
    check(gated > 0, "The sample course gates some prepared answers");

    // The guard itself: it does find the case seen in the third attended run.
    const auto* length = &*std::find_if(plot.faq.begin(), plot.faq.end(), [](const Faq& faq) { return faq.answer.find("威力 ÷ 24 秒") != std::string::npos
        && std::find(faq.keywords.begin(), faq.keywords.end(), "何秒") != faq.keywords.end(); });
    check(gives_away(*model, *length, *find_activity(plot, "weapon-power")), "The hit stop length answer is detected as giving away weapon-power");
    check(length->after == "weapon-power" || position(length->after) >= position("weapon-power"), "…and waits for weapon-power");

    // The rule in the classroom: before the activity is revealed the question waits for the teacher, after it the prepared answer is given.
    Room room;
    const auto aoi = room.join("あおい"), haruto = room.join("はると");
    room.console({{"type", "lesson.start"}});
    room.classroom.tick(room.now);
    const std::string question = "ヒットストップって、だいたい何秒くらい止めるのが普通なんですか？長すぎるとどうなるのかも知りたいです。";
    auto out = room.say(aoi, {{"type", "question"}, {"text", question}});
    const auto& early = status(out, aoi);
    check(early.at("state") == "waiting", "Before weapon-power is revealed, the hit stop length question waits for the teacher");
    const auto reply = find_type(out, "reply", aoi)->body.at("text").get<std::string>();
    check(reply.find("威力") == std::string::npos && reply.find("このあとの授業の問い") != std::string::npos, "The asker is told why, without the answer");
    check(find_type(out, "attention") && find_type(out, "attention")->body.at("reason") == "question_needs_teacher", "The operator is asked to answer");

    room.console({{"type", "activity.open"}, {"id", "weapon-power"}});
    room.console({{"type", "activity.reveal"}});
    out = room.say(haruto, {{"type", "question"}, {"text", "ヒットストップの長さは何秒がいいですか"}});
    check(status(out, haruto).at("state") == "waiting", "It also gives hitstop-design away, so it waits for that one too");
    room.console({{"type", "activity.open"}, {"id", "hitstop-design"}});
    room.console({{"type", "activity.reveal"}});
    out = room.say(haruto, {{"type", "question"}, {"text", "ヒットストップは何秒くらいの長さにするのがいいですか"}});
    check(status(out, haruto).at("state") == "answered" && status(out, haruto).at("answer").get<std::string>().find("威力 ÷ 24 秒") != std::string::npos,
        "Once both are revealed, the prepared answer is given");

    // The plot names an existing activity.
    auto document = Json::parse(R"({"id":"p","title":"t","sections":[{"id":"s","title":"s","activity":{"id":"a","kind":"open","prompt":"p","points":[{"id":"x","label":"x","keywords":["x"]}]}}],
        "faq":[{"keywords":["k"],"answer":"a","after":"nope"}]})");
    rejects([&] { parse_plot(document); }, "invalid_plot");
    document["faq"][0]["after"] = "a";
    check(parse_plot(document).faq[0].after == "a", "after names an activity of the plot");
}); }
