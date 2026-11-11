#include "lesson_fixture.hpp"
#include "assignment.hpp"
using namespace odeum;
using namespace odeum::lesson;
using Kind = AssignmentBoard::Kind;

int main() { return run([] {
    const auto plot = sample_plot();
    const auto model = make_provisional_model();
    const auto& plan = *find_assignment(plot, "restore-feel");
    const Millis stall = static_cast<Millis>(plan.stall_seconds) * 1000;

    AssignmentBoard board(plan, *model, 0);
    for (const auto* id : {"a", "b", "c", "d", "e"}) board.enroll(id, 0);
    check(board.monitor(1000).empty(), "Nothing to say right after assigning");
    check(board.brief().at("steps").size() == 6 && !board.brief().at("steps")[0].contains("hints"), "Hints are not sent with the brief");
    const auto auto_step = board.brief().at("steps")[2];
    check(auto_step.at("auto") == true && auto_step.at("check").at("variable") == "feel.hit_stop_last_seconds" && auto_step.at("check").at("op") == "gt"
        && !board.brief().at("steps")[0].contains("check"), "The brief says which variable an automatic step watches");

    // a works steadily, b never starts, c asks for help, d stalls, e finishes.
    board.report("a", 0, true, 10'000);
    board.report("c", 0, true, 10'000);
    board.report("d", 0, true, 10'000); board.report("d", 1, true, 20'000);
    for (std::size_t step = 0; step < plan.steps.size(); ++step) board.report("e", step, true, 30'000);
    rejects([&] { board.report("a", 6, true, 10'000); }, "invalid_step");
    rejects([&] { board.report("zz", 0, true, 10'000); }, "not_assigned");

    board.request_help("c", 40'000);
    auto found = board.monitor(40'000);
    check(found.size() == 2, "Help and completion are answered at once");
    const auto pick = [&](const std::string& student) { for (const auto& i : found) if (i.student == student) return i; throw std::runtime_error("No intervention for " + student); };
    check(pick("c").kind == Kind::hint && pick("c").step == 1 && pick("c").level == 0 && pick("c").text == plan.steps[1].hints[0], "First hint for the step the student is on");
    check(pick("e").kind == Kind::praise && pick("e").text.find(plan.extra) != std::string::npos, "Finishers get the stretch goal");
    check(board.monitor(41'000).empty(), "Interventions are not repeated");
    rejects([&] { board.request_help("c", 45'000); }, "rate_limited");

    // b is nudged after the grace period, once.
    found = board.monitor(AssignmentBoard::start_grace_ms);
    check(found.size() == 1 && found[0].student == "b" && found[0].kind == Kind::start_nudge, "Untouched assignment gets a nudge");

    // a and d stall on different steps: each gets a private hint for their own step.
    found = board.monitor(20'000 + stall);
    check(found.size() == 2 && pick("a").step == 1 && pick("d").step == 2 && pick("d").text == plan.steps[2].hints[0], "Stalled students get their own step's hint");
    check(board.overview(20'000 + stall).at("students").at("e").at("state") == "done", "Overview shows completion");

    // Hints escalate, then a person is called.
    found = board.monitor(20'000 + stall * 2);
    check(pick("d").kind == Kind::hint && pick("d").level == 1, "Second hint");
    found = board.monitor(20'000 + stall * 3);
    check(pick("d").kind == Kind::hint && pick("d").level == 2, "Third hint");
    found = board.monitor(20'000 + stall * 4);
    check(pick("d").kind == Kind::escalate, "Out of hints -> the teacher looks in person");
    check(board.overview(20'000 + stall * 4).at("students").at("d").at("state") == "needs_teacher", "Escalated state");
    found = board.monitor(20'000 + stall * 9);
    for (const auto& i : found) check(i.student != "d", "No more automatic hints after escalation");
    board.report("d", 2, true, 20'000 + stall * 9);
    check(board.overview(20'000 + stall * 9).at("students").at("d").at("state") == "working", "Progress clears the escalation");

    // Unticking a step reopens it and allows praise again later.
    board.report("e", 5, false, 900'000);
    board.report("e", 5, true, 901'000);
    found = board.monitor(901'000);
    check(pick("e").kind == Kind::praise, "Praise again after redoing a step");

    // When much of the class is stuck on one step, the lecturer speaks to everyone once.
    AssignmentBoard crowd(plan, *model, 0);
    for (const auto* id : {"a", "b", "c", "d", "e"}) { crowd.enroll(id, 0); crowd.report(id, 0, true, 1000); }
    crowd.report("e", 1, true, 2000);
    found = crowd.monitor(1000 + stall);
    check(found.size() == 1 && found[0].kind == Kind::class_hint && found[0].student.empty() && found[0].step == 1, "Shared stall -> one class-wide hint");
    check(crowd.monitor(1001 + stall).empty(), "The four are not also hinted privately");
    found = crowd.monitor(1000 + stall * 2);
    check(found.size() == 5, "Next stall goes back to private hints");
    for (const auto& i : found) check(i.kind == Kind::hint && (i.student == "e" ? i.level == 0 : i.level == 1), "Class hint counts as the first hint");

    // A late joiner gets the full grace period.
    AssignmentBoard late(plan, *model, 0);
    late.enroll("x", 500'000);
    check(late.monitor(500'000 + AssignmentBoard::start_grace_ms - 1).empty(), "Grace runs from arrival");
    check(late.monitor(500'000 + AssignmentBoard::start_grace_ms).size() == 1, "Then the nudge");
    late.withdraw("x");
    check(late.monitor(9'000'000).empty() && late.progress("x").at("done").empty(), "Withdrawn students are not monitored");
}); }
