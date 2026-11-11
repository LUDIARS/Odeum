#include "lesson_fixture.hpp"
#include "console_guard.hpp"
#include "school.hpp"
#include <filesystem>
#include <fstream>
using namespace odeum;
using namespace odeum::lesson;

int main() { return run([] {
    check(builtin_plots().size() == 1, "One built-in course");
    School school(builtin_plots(), std::shared_ptr<const LessonModel>(make_provisional_model()), 60);
    rejects([&] { school.open("nope"); }, "unknown_plot");
    const auto opened = school.open("action-game");
    check(opened.code.size() == 10 && opened.id == "c1", "Join code is 10 symbols");
    check(school.resolve(opened.code, 0) == "c1", "Code resolves to its class");
    auto typed = opened.code; for (auto& c : typed) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    check(school.resolve(typed.substr(0, 5) + "-" + typed.substr(5), 0) == "c1", "Lowercase and hyphens are accepted");
    check(school.catalog().at("classes")[0].at("code") == opened.code && school.catalog().at("plots")[0].at("id") == "action-game", "Catalog lists plots and classes");
    check(school.plot("action-game") && !school.plot("x"), "Plot lookup");

    // Wrong codes are throttled service-wide; right codes are not counted.
    for (std::size_t i = 0; i < School::max_failures_per_minute; ++i) rejects([&] { school.resolve("ZZZZZZZZZZ", 1000); }, "class_not_found");
    rejects([&] { school.resolve(opened.code, 1000); }, "rate_limited");
    check(school.resolve(opened.code, 62'000) == "c1", "Throttle clears after a minute");

    auto& classroom = school.classroom("c1");
    const auto admission = classroom.admit("あおい", "");
    check(admission.resume.size() == 32 && classroom.admit("あおい", admission.resume).student == admission.student, "Resume tokens are random and stable");
    classroom.connect(admission.student, 0);
    classroom.from_console({{"type", "lesson.start"}}, 0);
    const auto ticked = school.tick(0);
    check(ticked.size() == 1 && ticked[0].first == "c1" && find_type(ticked[0].second, "section"), "Ticking the school runs its classes");

    for (std::size_t i = 1; i < School::max_classes; ++i) school.open("action-game");
    rejects([&] { school.open("action-game"); }, "capacity");
    school.close("c1");
    rejects([&] { school.classroom("c1"); }, "unknown_class");
    rejects([&] { school.resolve(opened.code, 200'000); }, "class_not_found");
    rejects([&] { school.close("c1"); }, "unknown_class");
    check(random_join_code() != random_join_code() && random_token().size() == 32, "Random values");

    // Console guard: direct operation of this machine only.
    const ConsoleRequest direct{true, false, "127.0.0.1:4411", "http://127.0.0.1:4411", "same-origin"};
    check(judge_console(direct, 4411) == ConsoleDecision::allow, "Direct loopback request");
    auto request = direct; request.origin.clear(); request.fetch_site = "none";
    check(judge_console(request, 4411) == ConsoleDecision::allow, "Typing the address into the browser");
    request = direct; request.host = "localhost:4411"; request.origin = "http://localhost:4411";
    check(judge_console(request, 4411) == ConsoleDecision::allow, "localhost name");
    request = direct; request.peer_loopback = false;
    check(judge_console(request, 4411) == ConsoleDecision::not_loopback, "Remote peer");
    request = direct; request.forwarded = true;
    check(judge_console(request, 4411) == ConsoleDecision::forwarded, "Tunnelled request");
    request = direct; request.host = "lesson.example.com";
    check(judge_console(request, 4411) == ConsoleDecision::bad_host, "DNS rebinding");
    request = direct; request.host = "127.0.0.1:4410";
    check(judge_console(request, 4411) == ConsoleDecision::bad_host, "Wrong port in Host");
    request = direct; request.origin = "https://evil.example";
    check(judge_console(request, 4411) == ConsoleDecision::bad_origin, "Foreign origin");
    request = direct; request.origin.clear(); request.fetch_site = "cross-site";
    check(judge_console(request, 4411) == ConsoleDecision::cross_site, "Cross-site navigation");
    check(console_decision_name(ConsoleDecision::forwarded) == "forwarded", "Decision names");

    check(same_token("abc", "abc") && !same_token("abd", "abc") && !same_token("ab", "abc") && !same_token("", ""), "Token comparison; empty never matches");
    const auto path = (std::filesystem::temp_directory_path() / ("odeum-lesson-token-" + random_token())).string();
    const auto token = write_console_token(path);
    std::ifstream file(path); std::string stored; std::getline(file, stored);
    check(token.size() == 64 && stored == token, "Token file holds the token");
    check(write_console_token(path) != token, "Every start issues a new token");
#ifndef _WIN32
    const auto perms = std::filesystem::status(path).permissions();
    check((perms & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) == std::filesystem::perms::none, "Token file is owner-only");
#endif
    std::filesystem::remove(path);
    bool unreadable = false;
    try { load_plot_file("/nonexistent/plot.json"); } catch (const std::runtime_error&) { unreadable = true; }
    check(unreadable, "A missing plot file is an error, not an empty course");
}); }
