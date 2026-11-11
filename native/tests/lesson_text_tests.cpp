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
