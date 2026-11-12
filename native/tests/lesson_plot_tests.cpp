#include "lesson_fixture.hpp"
#include "plot.hpp"
using namespace odeum;
using namespace odeum::lesson;

namespace {
Json minimal() {
    return {{"id", "p"}, {"title", "T"}, {"sections", Json::array({{{"id", "s1"}, {"title", "S"}, {"points", {"a"}}}})}};
}
}
int main() { return run([] {
    const auto plot = sample_plot();
    check(plot.title == "アクションゲームの作り方" && plot.engine == "Ergo Studio", "Sample course identity");
    check(plot.minutes == 90 && plot.sections.size() == 9, "Sample course shape");
    int minutes = 0; for (const auto& section : plot.sections) minutes += section.minutes;
    check(minutes == plot.minutes, "Section minutes add up to the lesson length");

    // The three questions the lesson is built around.
    const auto hitstop = find_activity(plot, "hitstop-design"), knockback = find_activity(plot, "knockback-effects"), camera = find_activity(plot, "camera-open");
    check(hitstop && hitstop->kind == ActivityKind::text && !hitstop->model_answer.empty(), "Hit stop design has an answer");
    check(knockback && knockback->kind == ActivityKind::text, "Knockback question");
    check(camera && camera->kind == ActivityKind::open && camera->model_answer.empty(), "Camera question has no answer");
    const auto missing = find_activity(plot, "missing-feel");
    check(missing && missing->kind == ActivityKind::missing && !missing->code.empty(), "Missing-implementation activity shows code");
    const auto power = find_activity(plot, "weapon-power");
    check(power && power->multi && power->correct == std::vector<int>({1, 2}), "Weapon power affects knockback and hit stop");
    const auto assignment = find_assignment(plot, "restore-feel");
    check(assignment && assignment->steps.size() == 7 && assignment->stall_seconds == 150, "Assignment steps");
    check(find_section(plot, "camera") && !find_section(plot, "nope") && !find_activity(plot, "nope"), "Lookups");
    // Every choice question explains its answer; every demo says what to watch on the teacher's screen.
    for (const auto& section : plot.sections) {
        if (section.activity && section.activity->kind == ActivityKind::choice) check(!section.activity->explanation.empty(), "Choice questions carry an explanation");
        for (const auto& demo : section.demos) check(!demo.note.empty() && !demo.watch.empty(), "Demos carry what to watch");
    }
    // Automatic steps watch what the Ergo sample game publishes as "what really happened".
    for (const auto& step : assignment->steps) if (step.check)
        check(step.check->variable == "feel.hit_stop_last_seconds" || step.check->variable == "feel.knockback_last_impulse"
            || step.check->variable == "combat.enemy.armor_absorbed", "Checks watch the sample's observed values");
    // The rubric credits only what the reference sample removes (its six Setting switches).
    for (const auto& point : missing->points) check(point.id != "sound", "Sound is not one of the removed effects");
    check(missing->points.size() == 6, "Six removed effects, as the model answer says");

    const auto small = parse_plot(minimal());
    check(small.minutes == 60 && small.sections[0].minutes == 5 && !small.sections[0].activity, "Defaults");

    const auto broken = [](auto edit) { auto doc = minimal(); edit(doc); rejects([&] { parse_plot(doc); }, "invalid_plot"); };
    broken([](Json& d) { d.erase("title"); });
    broken([](Json& d) { d["id"] = "Has Space"; });
    broken([](Json& d) { d["sections"] = Json::array(); });
    broken([](Json& d) { d["minutes"] = 0; });
    broken([](Json& d) { d["sections"].push_back(d["sections"][0]); });                       // duplicate section id
    broken([](Json& d) { d["sections"][0]["activity"] = {{"id", "a"}, {"kind", "quiz"}, {"prompt", "?"}}; });
    broken([](Json& d) { d["sections"][0]["activity"] = {{"id", "a"}, {"kind", "choice"}, {"prompt", "?"}, {"choices", {"only"}}}; });
    broken([](Json& d) { d["sections"][0]["activity"] = {{"id", "a"}, {"kind", "choice"}, {"prompt", "?"}, {"choices", {"x", "y"}}, {"correct", {2}}}; });
    broken([](Json& d) { d["sections"][0]["activity"] = {{"id", "a"}, {"kind", "choice"}, {"prompt", "?"}, {"choices", {"x", "y"}}, {"correct", {0, 1}}}; });  // single choice, two answers
    broken([](Json& d) { d["sections"][0]["activity"] = {{"id", "a"}, {"kind", "text"}, {"prompt", "?"}}; });                         // no rubric
    broken([](Json& d) { d["sections"][0]["activity"] = {{"id", "a"}, {"kind", "open"}, {"prompt", "?"}, {"model_answer", "x"},
        {"points", {{{"id", "p"}, {"label", "L"}, {"keywords", {"k"}}}}}}; });                                                         // open cannot have an answer
    broken([](Json& d) { d["sections"][0]["activity"] = {{"id", "a"}, {"kind", "open"}, {"prompt", "?"}, {"explanation", "x"},
        {"points", {{{"id", "p"}, {"label", "L"}, {"keywords", {"k"}}}}}}; });                                                         // nor an explanation
    broken([](Json& d) { d["sections"][0]["activity"] = {{"id", "a"}, {"kind", "choice"}, {"prompt", "?"}, {"choices", {"x", "y"}},
        {"explanation", std::string(601, 'x')}}; });
    broken([](Json& d) { d["sections"][0]["activity"] = {{"id", "a"}, {"kind", "choice"}, {"prompt", "?"}, {"choices", {"x", "y"}}, {"explanation", 3}}; });
    broken([](Json& d) { d["sections"][0]["demos"] = {{{"feature", "f"}, {"watch", std::string(201, 'x')}}}; });
    const auto explained = [] { auto doc = minimal(); doc["sections"][0]["activity"] = {{"id", "a"}, {"kind", "choice"}, {"prompt", "?"},
        {"choices", {"x", "y"}}, {"correct", {0}}, {"explanation", " x だから "}}; doc["sections"][0]["demos"] = {{{"feature", "f"}, {"watch", "w"}}}; return parse_plot(doc); }();
    check(explained.sections[0].activity->explanation == "x だから" && explained.sections[0].demos[0].watch == "w", "Explanation and watch are read");
    broken([](Json& d) { d["sections"][0]["assignment"] = {{"id", "w"}, {"title", "W"}, {"steps", Json::array()}}; });
    broken([](Json& d) { d["sections"][0]["title"] = std::string(121, 'x'); });
    broken([](Json& d) { d["sections"][0]["points"] = {1}; });
    rejects([] { parse_plot(Json::array()); }, "invalid_plot");
}); }
