#include "lesson_fixture.hpp"
#include "assets.hpp"
#include "school.hpp"
#include "timeline.hpp"
using namespace odeum;
using namespace odeum::lesson;

namespace {
// The built-in course as its author wrote it, so tests can break one field at a time.
Json sample_document() {
    const auto asset = find_asset("/courses/action-game.plot.json");
    if (!asset) throw std::runtime_error("Sample course asset missing");
    return Json::parse(asset->body);
}
Json& sample_steps(Json& document) {
    for (auto& section : document["sections"]) if (section.contains("assignment")) return section["assignment"]["steps"];
    throw std::runtime_error("Sample course has no assignment");
}
}

int main() { return run([] {
    // ---- plot: what a tool may watch and switch ----
    const auto plot = sample_plot();
    check(plot.tool_features.at("hit_stop") == "feel.hit_stop" && plot.tool_features.at("super_armor") == "feel.super_armor"
        && plot.tool_features.at("effects") == "feel.all" && plot.tool_features.size() == 9, "Demo features name their variables");
    const auto& steps = find_assignment(plot, "restore-feel")->steps;
    check(steps[2].check && steps[2].check->variable == "feel.hit_stop_last_seconds" && steps[2].check->op == "gt" && steps[2].check->value == 0,
        "A step can carry a check");
    check(!steps[0].check && has_checked_steps(plot), "Steps without a check stay manual");
    {
        auto broken = sample_document();
        sample_steps(broken)[2]["check"]["op"] = "between";
        rejects([&] { parse_plot(broken); }, "invalid_plot");
        broken = sample_document();
        sample_steps(broken)[2]["check"]["value"] = "fast";      // ordering needs a number
        rejects([&] { parse_plot(broken); }, "invalid_plot");
        broken = sample_document();
        sample_steps(broken)[2]["check"]["variable"] = "feel hit stop";
        rejects([&] { parse_plot(broken); }, "invalid_plot");
        broken = sample_document();
        sample_steps(broken)[2]["check"] = {{"variable", "feel.mode"}, {"op", "eq"}, {"value", "heavy"}};
        check(parse_plot(broken).sections.size() == plot.sections.size(), "Equality may compare text");
        broken = sample_document();
        broken["tool"]["features"]["all"] = "feel.everything";  // reserved: "all" addresses every feature
        rejects([&] { parse_plot(broken); }, "invalid_plot");
        broken = sample_document();
        broken["tool"]["features"].erase("camera_shake");       // a demo now switches something unnamed
        rejects([&] { parse_plot(broken); }, "invalid_plot");
        broken = sample_document();
        broken.erase("tool");
        check(parse_plot(broken).tool_features.empty(), "A plot without tool bindings only announces its demos");
    }

    // ---- desk: link codes and tokens ----
    {
        unsigned codes = 0, tokens = 0;
        ToolDesk desk([&] { return "AAAAAAAAA" + std::to_string(codes++ % 10); }, [&] { return "token" + std::to_string(tokens++); });
        const ToolDesk::Holder aoi{"c1", "s1", ToolDesk::Role::student}, ren{"c1", "s2", ToolDesk::Role::student}, presenter{"c1", {}, ToolDesk::Role::presenter};
        const auto first = desk.issue(aoi, 0);
        const auto second = desk.issue(aoi, 10);
        rejects([&] { desk.redeem(first, 20); }, "link_not_found");                 // replaced by the newer code
        const auto token = desk.redeem("aaaaa-aaaa1", 20);                          // typed in lowercase with a hyphen
        check(second == "AAAAAAAAA1" && desk.holder(token).student == "s1" && desk.linked("c1", "s1") && !desk.linked("c1", "s2"), "Code becomes a token for its holder");
        rejects([&] { desk.redeem(second, 30); }, "link_not_found");                // single use
        const auto late = desk.issue(ren, 0);
        rejects([&] { desk.redeem(late, ToolDesk::link_ms); }, "link_not_found");   // expired
        rejects([&] { desk.holder("nope"); }, "tool_unlinked");

        // A third machine for the same student replaces the oldest of two.
        const auto token2 = desk.redeem(desk.issue(aoi, 100), 100), token3 = desk.redeem(desk.issue(aoi, 100), 100);
        rejects([&] { desk.holder(token); }, "tool_unlinked");
        check(desk.holder(token2).student == "s1" && desk.holder(token3).student == "s1", "Two machines per participant");

        const auto host = desk.redeem(desk.issue(presenter, 100), 100);
        check(desk.holder(host).role == ToolDesk::Role::presenter && desk.linked("c1", {}) && tool_role_name(desk.holder(host).role) == "presenter", "Presenter tool");
        desk.revoke_student("c1", "s1");
        rejects([&] { desk.holder(token2); }, "tool_unlinked");
        check(desk.holder(host).student.empty(), "Removing a student leaves the others linked");
        const auto pending = desk.issue(ren, 200);
        desk.revoke_class("c1");
        rejects([&] { desk.holder(host); }, "tool_unlinked");
        rejects([&] { desk.redeem(pending, 200); }, "link_not_found");              // unused codes die with the class

        // The participant's page is told what linked, with the tool's own name kept to a safe form.
        ToolDesk named([&] { return "CCCCCCCCC" + std::to_string(codes++ % 10); }, [&] { return "named" + std::to_string(tokens++); });
        check(named.link_state("c1", "s1") == Json{{"type", "tool.unlinked"}}, "Nothing linked yet");
        named.redeem(named.issue(aoi, 0), 0, "ergo-studio");
        auto linked = named.link_state("c1", "s1");
        check(linked.at("type") == "tool.linked" && linked.at("count") == 1 && linked.at("tools")[0] == "ergo-studio", "Linked, with the tool's name");
        named.redeem(named.issue(aoi, 0), 0, "<b>先生</b> says: hand in now\n");
        linked = named.link_state("c1", "s1");
        check(linked.at("count") == 2 && linked.at("tools")[1] == "bb says hand in now", "A declared name keeps only plain ASCII and no markup");
        check(tool_display_name("") == "名前の無い道具" && tool_display_name(std::string(80, 'x')).size() == 40, "Empty or long names are bounded");
        check(named.link_state("c1", "s2").at("type") == "tool.unlinked", "Another student's tools are not theirs");
        check(named.revoke_student("c1", "s1") && named.link_state("c1", "s1").at("type") == "tool.unlinked" && !named.revoke_student("c1", "s1"),
            "Revoking reports whether anything was linked, and the page then reads unlinked");

        // Wrong codes are throttled service-wide, so a code cannot be found by trying.
        ToolDesk guarded([] { return std::string("BBBBBBBBBB"); }, [] { return std::string("token"); });
        for (std::size_t i = 0; i < ToolDesk::max_failures_per_minute; ++i) rejects([&] { guarded.redeem("ZZZZZZZZZZ", 1000); }, "link_not_found");
        const auto good = guarded.issue(ren, 1000);
        rejects([&] { guarded.redeem(good, 1000); }, "rate_limited");
        check(guarded.redeem(good, 62'000) == "token", "Throttle clears after a minute");
    }

    // ---- classroom: what a tool reads and reports ----
    School school(builtin_plots(), std::shared_ptr<const LessonModel>(make_provisional_model()), 60);
    const auto opened = school.open("action-game");
    auto& room = school.classroom(opened.id);
    const auto aoi = room.admit("あおい", "").student;
    room.connect(aoi, 0);

    auto state = room.tool_state(aoi);
    check(state.at("role") == "student" && state.at("assignment").is_null() && !state.contains("demo")
        && state.at("features").at("knockback") == "feel.knockback" && state.at("class").at("title") == plot.title, "Student tool before the assignment");
    rejects([&] { room.from_tool(aoi, {{"assignment_id", "restore-feel"}, {"step", 2}, {"done", true}}, 0); }, "not_assigned");

    room.from_console({{"type", "assignment.assign"}, {"id", "restore-feel"}}, 1000);
    state = room.tool_state(aoi);
    const auto& listed = state.at("assignment").at("steps");
    check(state.at("assignment").at("id") == "restore-feel" && listed.size() == steps.size() && listed[2].at("done") == false
        && listed[2].at("check") == Json({{"variable", "feel.hit_stop_last_seconds"}, {"op", "gt"}, {"value", 0}}) && !listed[0].contains("check"),
        "Student tool sees the steps and their checks");

    const auto out = room.from_tool(aoi, {{"assignment_id", "restore-feel"}, {"step", 2}, {"done", true}}, 2000);
    const auto told = find_type(out, "progress", aoi);
    check(told && told->body.at("assignment").at("done")[2] == true, "The student's page is told what the tool saw");
    check(room.tool_state(aoi).at("assignment").at("steps")[2].at("done") == true, "Reported steps read back as done");
    rejects([&] { room.from_tool(aoi, {{"assignment_id", "restore-feel"}, {"step", 99}, {"done", true}}, 2000); }, "invalid_step");
    rejects([&] { room.from_tool("s99", {{"assignment_id", "restore-feel"}, {"step", 1}, {"done", true}}, 2000); }, "unknown_student");
    check(room.console_snapshot(2000).at("class").at("demo_tool") == true, "Console learns that demos can be switched by a tool");

    // The presenter's tool follows the demos, including after it reconnects.
    state = room.tool_state({});
    check(state.at("role") == "presenter" && state.at("assignment").is_null() && state.at("demo").at("seq") == 0 && state.at("demo").at("states").empty(),
        "Presenter tool before any demo");
    room.from_console({{"type", "lesson.start"}}, 3000);
    const auto timeline = build_timeline(plot, *make_provisional_model());
    // first_demo: the opening demo (every effect on); hit_stop_on: the hit-stop section turning it on alone.
    Millis first_demo = -1, hit_stop_on = -1;
    for (const auto& cue : timeline.cues) {
        if (cue.kind != CueKind::demo) continue;
        if (first_demo < 0) first_demo = cue.at;
        if (hit_stop_on < 0 && cue.detail.at("feature") == "hit_stop" && cue.detail.at("enabled") == true) hit_stop_on = cue.at;
    }
    check(first_demo >= 0 && hit_stop_on > first_demo, "Sample course has demos");
    room.tick(3000 + first_demo);
    state = room.tool_state({});
    check(state.at("demo").at("seq") == 1 && state.at("demo").at("states") == Json({{"effects", true}}),
        "The opening demo switches every effect on (feel.all), and nothing else");
    room.tick(3000 + hit_stop_on);
    state = room.tool_state({});
    check(state.at("demo").at("seq").get<int>() >= 3 && state.at("demo").at("states") == Json({{"effects", false}, {"hit_stop", true}}),
        "Later demos leave the latest state of each feature");

    // A seek skips cues; the presenter's game must still end up as the skipped demos left it.
    const auto seq = state.at("demo").at("seq").get<int>();
    room.from_console({{"type", "lesson.seek"}, {"position_ms", first_demo + 1}}, 3000 + hit_stop_on);
    state = room.tool_state({});
    check(state.at("demo").at("seq").get<int>() > seq && state.at("demo").at("states") == Json({{"effects", true}}),
        "Seeking back restores what the demos had switched by then");
    room.from_console({{"type", "lesson.seek"}, {"position_ms", 0}}, 3000 + hit_stop_on);
    check(room.tool_state({}).at("demo").at("states").empty(), "Before the first demo nothing is switched");

    // "all" switches every named feature (a variant of the sample whose first demo says "all").
    {
        auto doc = sample_document();
        doc["id"] = "all-demo";
        doc["sections"][0]["demos"] = Json::array({{{"feature", "all"}, {"enabled", true}, {"note", "n"}, {"watch", "w"}, {"after", 0}}});
        auto catalog = builtin_plots();
        catalog.push_back(parse_plot(doc));
        School variant(catalog, std::shared_ptr<const LessonModel>(make_provisional_model()), 60);
        auto& all_room = variant.classroom(variant.open("all-demo").id);
        all_room.from_console({{"type", "lesson.start"}}, 0);
        Millis all_at = -1;
        for (const auto& cue : build_timeline(*variant.plot("all-demo"), *make_provisional_model()).cues)
            if (cue.kind == CueKind::demo && all_at < 0) all_at = cue.at;
        all_room.tick(all_at);
        const auto all_state = all_room.tool_state({});
        Json every = Json::object();
        for (const auto& [name, variable] : plot.tool_features) every[name] = true;
        check(all_at >= 0 && all_state.at("demo").at("seq") == 1 && all_state.at("demo").at("states") == every, "\"all\" switches every named feature");
    }

    // Removal ends the student's tool reports as well.
    room.from_console({{"type", "student.eject"}, {"student", aoi}}, 4000);
    rejects([&] { room.from_tool(aoi, {{"assignment_id", "restore-feel"}, {"step", 3}, {"done", true}}, 4000); }, "ejected");
    school.tools().issue({opened.id, {}, ToolDesk::Role::presenter}, 0);
    const auto host = school.tools().redeem(school.tools().issue({opened.id, {}, ToolDesk::Role::presenter}, 0), 0);
    school.close(opened.id);
    rejects([&] { school.tools().holder(host); }, "tool_unlinked");
}); }
