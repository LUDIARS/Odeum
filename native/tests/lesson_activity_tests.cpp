#include "lesson_fixture.hpp"
#include "activity.hpp"
#include <algorithm>
using namespace odeum;
using namespace odeum::lesson;

int main() { return run([] {
    const auto plot = sample_plot();
    const auto model = make_provisional_model();

    // "What is missing?": graded against the removed implementations.
    Activity missing(*find_activity(plot, "missing-feel"), *model);
    check(!missing.prompt().contains("points") && !missing.prompt().contains("model_answer"), "Prompt never leaks the answer");
    missing.answer("s1", {{"text", "当たった瞬間に一瞬止まるのと、カメラが揺れるのがない"}});
    missing.answer("s2", {{"text", "数字が出ない"}});
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
    check(mine.at("text") == "当たった瞬間に一瞬止まるのと、カメラが揺れるのがない" && missing.submitted("s1").at("text") == mine.at("text"), "Personal result carries the answer it grades");
    check(missing.personal("s2").at("correct") == false && missing.personal("s9").at("answered") == false, "Partial and absent students");
    const auto partial = missing.personal("s2").at("feedback").get<std::string>();
    check(partial.find("「ダメージの数字」") != std::string::npos && partial.find("2 つ要ります") != std::string::npos, "Partial feedback states what matched and what the rule needs");
    check(mine.at("feedback").get<std::string>().find("正解の扱いです") != std::string::npos, "A correct answer is told why it counts");
    // Sound is not one of the six effects the sample removes, so it earns nothing and the key says so.
    Activity sound(*find_activity(plot, "missing-feel"), *model);
    sound.answer("s1", {{"text", "音がない"}});
    sound.reveal();
    check(sound.personal("s1").at("matched").empty() && sound.results().at("model_answer").get<std::string>().find("音はこのサンプルにはもともと無い") != std::string::npos,
        "Rubric, model answer and the sample agree on what was removed");
    check(sound.personal("s1").at("feedback").get<std::string>().find("見落としやすい") == std::string::npos, "No claim about what others tend to miss");

    // Choice with several right options.
    Activity power(*find_activity(plot, "weapon-power"), *model);
    power.answer("s1", {{"choices", {2, 1}}});
    power.answer("s2", {{"choices", {0}}});
    rejects([&] { power.answer("s3", {{"choices", Json::array()}}); }, "invalid_answer");
    rejects([&] { power.answer("s3", {{"choices", {4}}}); }, "invalid_answer");
    rejects([&] { power.answer("s3", {{"choices", {1, 1}}}); }, "invalid_answer");
    rejects([&] { power.answer("s3", {{"text", "ノックバック"}}); }, "invalid_answer");
    check(power.tally().at("counts") == std::vector<int>({1, 1, 1, 0}), "Choice counts");
    check(!power.prompt().contains("explanation") && !power.tally().contains("explanation"), "The explanation stays hidden until the reveal");
    power.reveal();
    check(power.results().at("correct") == std::vector<int>({1, 2}), "Answer key shown at reveal");
    check(power.results().at("explanation").get<std::string>().find("威力 ÷ 24") != std::string::npos, "The author's explanation is shown to everyone at the reveal");
    check(power.personal("s1").at("feedback") == "正解です。理由はクラスの結果の解説を見てください。", "Personal feedback points to the explanation");
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
    check(open.at("has_answer") == false && !open.contains("model_answer") && !open.contains("explanation") && open.at("correct_count") == 0, "Open questions have no right answer");
    const auto personal = camera.personal("s1");
    check(!personal.contains("correct") && personal.at("matched").size() == 4, "No verdict, perspectives only");
    const auto said = personal.at("feedback").get<std::string>();
    check(said.find("正解はありません") != std::string::npos && said.find("理由を言えることが大事") == std::string::npos, "No right answer, and no judgement of the reason");
    // Only perspectives another student really gave are mentioned: s2 spoke of control only.
    check(said.find("「誰が動かすか (自動か操作か)」") != std::string::npos && said.find("追い方") == std::string::npos && said.find("ロックオン") == std::string::npos,
        "Other perspectives come from other students' answers, never from the unused rubric");
    const auto second = camera.personal("s2").at("feedback").get<std::string>();
    check(second.find("「揺れの使い方」") != std::string::npos && second.find("追い方") == std::string::npos, "The other side sees s1's perspectives");
    Activity alone(*find_activity(plot, "camera-open"), *model);
    alone.answer("s1", {{"text", "前を広めに映す。画面外の攻撃が見えないのは困る"}});
    alone.reveal();
    const auto solo = alone.personal("s1").at("feedback").get<std::string>();
    check(solo.find("ほかの人") == std::string::npos && alone.personal("s1").at("matched").size() >= 1, "Nobody else answered: nothing is said about others");
    check(alone.personal("s1").at("matched")[0] == "view", "Natural wording counts for the view perspective");

    // Realistic sentences from students meet the rubric (keyword gaps found in a real attendance).
    const auto grade = [&](const char* id, const char* text) { return model->grade(*find_activity(plot, id), text).matched; };
    const auto has = [](const std::vector<std::string>& ids, const char* id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    check(has(grade("camera-open", "前を広めに映す"), "view") && has(grade("camera-open", "画面外の攻撃が見えないと理不尽"), "view"), "映す範囲: 広めに / 見えない");
    check(has(grade("camera-open", "画面の外が映らないと困る"), "view"), "映す範囲: 映らない");
    check(has(grade("camera-open", "プレイヤーを少し遅れて追いかけるカメラ"), "follow") && has(grade("camera-open", "揺らしすぎると酔うので弱くできるように"), "comfort"), "追い方, 酔いにくさ");
    check(has(grade("camera-open", "ｶﾒﾗは自分で回せるといい"), "control"), "Halfwidth katakana and 回せる");
    check(has(grade("missing-feel", "当たっても止まらないし、敵が飛ばない"), "hit_stop") && has(grade("missing-feel", "当たっても止まらないし、敵が飛ばない"), "knockback"), "止まらない / 飛ばない");
    check(has(grade("missing-feel", "画面が揺れない、火花が光らない"), "camera_shake") && has(grade("missing-feel", "画面が揺れない、火花が光らない"), "hit_effect"), "揺れない / 光らない");
    check(has(grade("missing-feel", "HPが減っていく演出がない"), "hp_bar"), "体力ゲージ: 減っていく");
    check(has(grade("hitstop-design", "強い攻撃ほど長めに止める"), "scale") && has(grade("hitstop-design", "強い攻撃ほど長めに止める"), "length"), "長めに / 強い攻撃");
    check(has(grade("hitstop-design", "UIとタイマーは止めずに動かし続ける"), "exempt") && has(grade("hitstop-design", "連続で当たったら上書きする"), "overlap"), "止めないもの / 重なり");
    check(has(grade("hitstop-design", "長すぎるともっさりして操作が重く感じる"), "tempo"), "テンポ");
    check(has(grade("knockback-effects", "相手が離れて次の攻撃が届かなくなる"), "distance") && has(grade("knockback-effects", "相手が離れて次の攻撃が届かなくなる"), "combo"), "離れて / 届かなく");
    check(has(grade("knockback-effects", "壁にめり込んだり崖から落ちたりする"), "terrain") && has(grade("knockback-effects", "のけぞって動けなくなる"), "stun"), "地形 / 硬直");
    check(has(grade("knockback-effects", "敵がまとまっていると巻き込んでばらける"), "formation") && has(grade("knockback-effects", "軽い敵はよく滑る"), "physics"), "並び / 物理");
    check(!has(grade("knockback-effects", "敵が重なって動けない"), "physics"), "重なる is not 重い");
    // Over-broad single words: naming input or sound as something not to stop is not about tempo.
    const auto exempted = grade("hitstop-design", "決めるのは、何秒止めるかと、何を止めるか（キャラだけか画面全体か）と、止めちゃいけないもの（入力の受付やBGM、"
        "止めた時間を数えるタイマー）だと思います。自分なら0.05〜0.1秒くらいで、強い攻撃ほど長くして、当てた側と当たった側だけ止めます。");
    check(!has(exempted, "tempo") && has(exempted, "exempt") && has(exempted, "length") && has(exempted, "scope") && has(exempted, "scale"),
        "入力の受付 / BGM are things not to stop, not tempo");
    check(has(grade("hitstop-design", "長く止めると操作感が悪くなる"), "tempo") && has(grade("hitstop-design", "入力が遅れて感じる"), "tempo"), "Tempo still found when it is meant");
    check(!has(grade("hitstop-design", "音を出すタイミングだけ考える"), "tempo") && !has(grade("missing-feel", "サーバーのオーバーヘッド"), "hp_bar"), "バー is not a gauge");
    check(!has(grade("knockback-effects", "逆に言えば、届かなくなる"), "direction") && !has(grade("knockback-effects", "中途半端な距離"), "terrain"), "逆に / 中途半端 are not direction / terrain");
    check(has(grade("knockback-effects", "背後から当てると逆向きに飛ぶ"), "direction") && has(grade("knockback-effects", "画面端で止まる"), "terrain"), "…while 逆向き / 画面端 still count");
    check(!has(grade("camera-open", "引き換えに視点が固定される"), "view") && !has(grade("camera-open", "自分で作るならこうする"), "control"), "引き換え / 自分で作る are not view / control");
}); }
