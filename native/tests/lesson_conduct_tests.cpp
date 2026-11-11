#include "check.hpp"
#include "conduct.hpp"
using namespace odeum;
using namespace odeum::lesson;
using Action = Conduct::Action;
using Standing = Conduct::Standing;

int main() { return run([] {
    Conduct conduct;
    check(conduct.standing("s", 0) == Standing::good && conduct.may_post("s", 0), "Unknown students are in good standing");

    // Noise: warn first, lock at three points.
    auto verdict = conduct.disruption("s", 1, 1000);
    check(verdict.action == Action::warn && conduct.standing("s", 1000) == Standing::warned && conduct.may_post("s", 1000), "First disruption is a warning");
    check(conduct.disruption("s", 1, 2000).action == Action::none, "Second adds up silently");
    verdict = conduct.disruption("s", 1, 3000);
    check(verdict.action == Action::lock && verdict.locked_until == 3000 + Conduct::first_lock_ms, "Third locks for a minute");
    check(!conduct.may_post("s", 3000 + Conduct::first_lock_ms - 1) && conduct.standing("s", 4000) == Standing::locked, "Locked students cannot post");
    check(conduct.expire(3000 + Conduct::first_lock_ms - 1).empty(), "Lock holds until its time");
    check(conduct.expire(3000 + Conduct::first_lock_ms) == std::vector<std::string>({"s"}), "Lock expires once");
    check(conduct.expire(3000 + Conduct::first_lock_ms).empty() && conduct.may_post("s", 3000 + Conduct::first_lock_ms), "And only once");

    // Coming back and continuing: the next lock is longer, the third removes.
    verdict = conduct.disruption("s", 1, 70'000);
    check(verdict.action == Action::lock && verdict.locked_until == 70'000 + 2 * Conduct::first_lock_ms, "Second lock doubles");
    conduct.expire(70'000 + 2 * Conduct::first_lock_ms);
    verdict = conduct.disruption("s", 1, 200'000);
    check(verdict.action == Action::eject && conduct.ejected("s") && !conduct.may_post("s", 200'000), "Third lockout removes the student");
    check(conduct.disruption("s", 2, 201'000).action == Action::none, "Nothing further happens to an ejected student");
    conduct.readmit("s");
    check(!conduct.ejected("s") && conduct.standing("s", 202'000) == Standing::good, "Readmission is a clean slate");

    // Abuse counts double and reaches removal while locked.
    Conduct harsh;
    check(harsh.disruption("a", 2, 0).action == Action::warn, "Abuse warns first");
    check(harsh.disruption("a", 2, 10).action == Action::lock, "Then locks");
    check(harsh.disruption("a", 2, 20).action == Action::eject, "Six points removes even while locked");

    // Strikes are forgotten after ten quiet minutes.
    Conduct patient;
    patient.disruption("p", 1, 0); patient.disruption("p", 1, 1000);
    check(patient.points("p", 2000) == 2, "Points accumulate");
    check(patient.points("p", Conduct::memory_ms + 1000) == 0 && patient.standing("p", Conduct::memory_ms + 1000) == Standing::good, "And expire");
    check(patient.disruption("p", 1, Conduct::memory_ms + 2000).action == Action::none, "A later slip does not lock");

    // Flooding: nine posts in ten seconds is one strike, not one per message.
    Conduct flood;
    for (int i = 0; i < 8; ++i) check(flood.post("f", i * 100).action == Action::none, "Eight quick posts are fine");
    check(flood.post("f", 900).action == Action::warn && flood.points("f", 900) == 1, "The ninth is a strike");
    check(flood.post("f", 1000).action == Action::none, "The burst is counted once");
    Conduct slow;
    for (int i = 0; i < 30; ++i) check(slow.post("f", i * 2000).action == Action::none, "Steady posting is never flooding");

    // Off-topic remarks are tolerated a few times a minute.
    Conduct chatty;
    for (int i = 0; i < 3; ++i) check(chatty.off_topic("c", i * 1000).action == Action::none, "Three asides are fine");
    check(chatty.off_topic("c", 4000).action == Action::warn, "The fourth in a minute is a strike");
    Conduct spaced;
    for (int i = 0; i < 10; ++i) check(spaced.off_topic("c", i * 61'000).action == Action::none, "Asides a minute apart never add up");

    // Operator controls.
    Conduct manual;
    verdict = manual.lock("m", 30'000, 0, "手動");
    check(verdict.action == Action::lock && verdict.locked_until == 30'000 && manual.locked_until("m") == 30'000, "Manual lock uses the given duration");
    manual.unlock("m");
    check(manual.may_post("m", 1000) && manual.points("m", 1000) == 0 && manual.expire(40'000).empty(), "Unlock restores at once");
    check(manual.lock("m", 99'999'999, 0, "").locked_until == Conduct::max_lock_ms, "Lock length is capped");
    check(manual.eject("m", "手動").action == Action::eject && manual.standing("m", 0) == Standing::ejected, "Manual removal");
    check(manual.lock("m", 1000, 0, "").action == Action::none, "Cannot lock someone who is gone");
    check(standing_name(Standing::locked) == "locked", "Standing names");
}); }
