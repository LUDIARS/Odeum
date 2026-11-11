#include "lesson_fixture.hpp"
#include "activity.hpp"
using namespace odeum;
using namespace odeum::lesson;

int main() { return run([] {
    const auto plot = sample_plot();
    const auto model = make_provisional_model();

    // "What is missing?": graded against the removed implementations.
    Activity missing(*find_activity(plot, "missing-feel"), *model);
    check(!missing.prompt().contains("points") && !missing.prompt().contains("model_answer"), "Prompt never leaks the answer");
    missing.answer("s1", {{"text", "当たった瞬間に一瞬止まるのと、カメラが揺れるのがない"}});
    missing.answer("s2", {{"text", "音がない"}});
    missing.answer("s3", {{"text", "よくわからない"}});
    missing.answer("s3", {{"text", "敵が吹っ飛ばない"}});
    check(missing.answered_count() == 3 && missing.answered("s3") && !missing.answered("s9"), "Later answers replace earlier ones");
    rejects([&] { missing.answer("s4", {{"text", ""}}); }, "invalid_answer");
    rejects([&] { missing.answer("s4", {{"choices", {0}}}); }, "invalid_answer");
    rejects([&] { missing.answer("s4", {{"text", std::string(401, 'a')}}); }, "invalid_answer");
    const auto tally = missing.tally();
    check(tally.at("answered") == 3 && !tally.contains("highlights") && !tally.contains("model_answer"), "Tally is counts only");
    missing.close();
    rejects([&] { missing.answer("s4", {{"text", "遅い"}}); }, "activity_closed");
    missing.reveal();
    const auto results = missing.results();
    check(results.at("has_answer") == true && results.contains("model_answer") && results.at("correct_count") == 1, "Two points are needed to get it");
    check(results.at("highlights").size() == 3 && results.at("highlights")[0] == "当たった瞬間に一瞬止まるのと、カメラが揺れるのがない", "Best-covering answer first, no names");
    const auto mine = missing.personal("s1");
    check(mine.at("correct") == true && mine.at("matched").size() == 2, "Personal result lists matched points");
    check(missing.personal("s2").at("correct") == false && missing.personal("s9").at("answered") == false, "Partial and absent students");

    // Choice with several right options.
    Activity power(*find_activity(plot, "weapon-power"), *model);
    power.answer("s1", {{"choices", {2, 1}}});
    power.answer("s2", {{"choices", {0}}});
    rejects([&] { power.answer("s3", {{"choices", Json::array()}}); }, "invalid_answer");
    rejects([&] { power.answer("s3", {{"choices", {4}}}); }, "invalid_answer");
    rejects([&] { power.answer("s3", {{"choices", {1, 1}}}); }, "invalid_answer");
    rejects([&] { power.answer("s3", {{"text", "ノックバック"}}); }, "invalid_answer");
    check(power.tally().at("counts") == std::vector<int>({1, 1, 1, 0}), "Choice counts");
    power.reveal();
    check(power.results().at("correct") == std::vector<int>({1, 2}), "Answer key shown at reveal");
    check(power.personal("s1").at("correct") == true && power.personal("s2").at("correct") == false, "Order of chosen options does not matter");

    // Single choice rejects two selections.
    Activity placement(*find_activity(plot, "enemy-placement"), *model);
    rejects([&] { placement.answer("s1", {{"choices", {0, 1}}}); }, "invalid_answer");

    // Open question: no verdict, answers are grouped by perspective.
    Activity camera(*find_activity(plot, "camera-open"), *model);
    camera.answer("s1", {{"text", "敵が見えるように引きにして、揺れは設定で切れるようにする"}});
    camera.answer("s2", {{"text", "自分で操作したい"}});
    camera.reveal();
    const auto open = camera.results();
    check(open.at("has_answer") == false && !open.contains("model_answer") && open.at("correct_count") == 0, "Open questions have no right answer");
    const auto personal = camera.personal("s1");
    check(!personal.contains("correct") && personal.at("matched").size() == 4, "No verdict, perspectives only");
    check(personal.at("feedback").get<std::string>().find("正解はありません") != std::string::npos, "Feedback says there is no right answer");
}); }
