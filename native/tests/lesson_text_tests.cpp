#include "check.hpp"
#include "text.hpp"
using namespace odeum;
using namespace odeum::lesson;

int main() { return run([] {
    // Folding makes spelling variants of the same word compare equal.
    check(fold("ヒット ストップ！") == fold("ひっとすとっぷ"), "Katakana, spaces and punctuation fold away");
    check(fold("ＨＰバー") == fold("hpばー"), "Fullwidth ASCII narrows and lowercases");
    check(fold("、。「」・ ！？").empty(), "Punctuation only folds to nothing");
    check(mentions(fold("カメラが揺れるのが好き"), "揺れ"), "Keyword found inside a sentence");
    check(!mentions(fold("カメラが好き"), "揺れ"), "Absent keyword not found");
    check(!mentions(fold("何か"), ""), "Empty keyword never matches");
    check(mentions_any(fold("ノックバックで落ちる"), {"壁", "落ち"}), "Any keyword matches");
    check(fold("ﾋｯﾄｽﾄｯﾌﾟ") == fold("ヒットストップ") && fold("ｶﾞｰﾄﾞ") == fold("ガード"), "Halfwidth katakana and its voiced marks fold");
    check(fold("ウ\u3099") == fold("ヴ") && fold("ハ\u309a") == fold("パ"), "Combining voiced marks fold");
    check(mentions(fold("ｶﾒﾗが揺れる"), "カメラ"), "Halfwidth student text matches a fullwidth keyword");

    // Okurigana: dictionary-form keywords match what students actually write.
    check(mentions(fold("前を広めに映す"), "広く"), "広く matches 広めに");
    check(mentions(fold("画面外の攻撃が見えない"), "見える"), "見える matches 見えない");
    check(mentions(fold("相手が後ろに下がった"), "下がる"), "下がる matches 下がった");
    check(mentions(fold("当たった敵を押し返して"), "押し返す"), "押し返す matches 押し返して");
    check(mentions(fold("画面を揺らさない方がいい"), "揺らす"), "揺らす matches 揺らさない");
    check(mentions(fold("重さで飛び方が変わる"), "重い") && mentions(fold("重くすると飛ばない"), "重い"), "重い matches 重さ and 重く");
    check(mentions(fold("短めに止める"), "短く") && mentions(fold("止める長さ"), "長く"), "Adjective stems");
    check(mentions(fold("敵を追って"), "追う") && mentions(fold("敵を追わない"), "追う"), "う verbs: 追って, 追わない");
    check(!mentions(fold("広告を出す"), "広く"), "A kanji stem needs an inflection after it");
    check(!mentions(fold("敵が重なって動けない"), "重い"), "重い does not match 重なる");
    check(!mentions(fold("届かせる"), "届かない"), "Only the last kana of a dictionary form is dropped");
    check(!mentions(fold("見たことがない"), "見える"), "見える does not become 見");

    check(similarity(fold("ヒットストップは何秒がいいですか"), fold("ヒットストップは何秒が良いですか？")) > 0.6, "Near-identical questions are similar");
    check(similarity(fold("ヒットストップは何秒がいいですか"), fold("カメラは誰が動かすべきですか")) < 0.2, "Different questions are not");
    check(similarity(fold("あ"), fold("あ")) == 1.0, "Single identical scalar");
    check(similarity(fold(""), fold("")) == 0.0, "Empty strings are not similar");

    check(longest_run(decode_utf8("wwwwww")) == 6, "ASCII run");
    check(longest_run(decode_utf8("ああああ草")) == 4, "Multibyte run");
    check(longest_run(decode_utf8("")) == 0, "Empty run");
    check(encode_utf8(decode_utf8("aあ𠮷")) == "aあ𠮷", "UTF-8 round trip across 1, 3 and 4 byte scalars");
    check(trim("  a b \n") == "a b", "Trim keeps inner spaces");
    check(trim(" \t").empty(), "Blank trims to empty");
    rejects([] { fold("\xff"); }, "invalid_message");
}); }
