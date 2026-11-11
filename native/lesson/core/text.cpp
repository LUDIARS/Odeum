#include "text.hpp"
#include <odeum/message.hpp>
#include <algorithm>
#include <array>
#include <set>

namespace odeum::lesson {
std::u32string decode_utf8(std::string_view text) {
    utf8_length(text);  // validates; the loop below can then assume well-formed input
    std::u32string out;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i++]);
        if (c < 0x80) { out.push_back(c); continue; }
        const unsigned extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
        char32_t cp = c & (0x3f >> extra);
        for (unsigned n = 0; n < extra; ++n) cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 0x3f);
        out.push_back(cp);
    }
    return out;
}
std::string encode_utf8(std::u32string_view text) {
    std::string out;
    for (const char32_t cp : text) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { out.push_back(static_cast<char>(0xc0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
        else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }
    return out;
}
namespace {
bool separator(char32_t c) {
    if (c <= 0x20 || c == 0x7f || c == 0x3000) return true;                      // controls, spaces
    if (c < 0x80) return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'));     // ASCII punctuation
    return (c >= 0x3001 && c <= 0x3003) || (c >= 0x3008 && c <= 0x3011) || c == 0x30fb  // 、。「」・
        || (c >= 0x2000 && c <= 0x206f);                                          // general punctuation
}
}
namespace {
// Halfwidth katakana (U+FF66-FF9D) as fullwidth katakana; IMEs and phones still produce them.
constexpr std::u32string_view halfwidth_kana =
    U"ヲァィゥェォャュョッーアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワン";
bool kanji(char32_t c) { return (c >= 0x4e00 && c <= 0x9fff) || (c >= 0x3400 && c <= 0x4dbf) || c == 0x3005; }
bool hiragana(char32_t c) { return c >= 0x3041 && c <= 0x3096; }
// Applies a (halfwidth or combining) voiced / semi-voiced mark to the katakana just before it.
bool voice(std::u32string& out, bool semi) {
    if (out.empty()) return false;
    char32_t& c = out.back();
    constexpr std::u32string_view voiceable = U"かきくけこさしすせそたちつてとはひふへほ";
    if (!semi && c == U'う') { c = U'ゔ'; return true; }
    if (voiceable.find(c) == std::u32string_view::npos || (semi && !(c >= U'は' && c <= U'ほ'))) return false;
    c += semi ? 2 : 1;
    return true;
}
}
std::u32string fold(std::string_view text) {
    std::u32string out;
    for (char32_t c : decode_utf8(text)) {
        if (c >= 0xff66 && c <= 0xff9d) c = halfwidth_kana[c - 0xff66];
        if ((c == 0xff9e || c == 0x3099) && voice(out, false)) continue;   // ﾞ and the combining dakuten
        if ((c == 0xff9f || c == 0x309a) && voice(out, true)) continue;    // ﾟ and the combining handakuten
        if (c >= 0xff01 && c <= 0xff5e) c -= 0xfee0;       // fullwidth ASCII
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c >= 0x30a1 && c <= 0x30f6) c -= 0x60;         // katakana -> hiragana
        if (!separator(c)) out.push_back(c);
    }
    return out;
}
namespace {
// Okurigana: a keyword written in dictionary form ("見える", "広く", "下がる") also matches its other
// forms ("見えない", "広め", "下がった"). The last kana is dropped; when that leaves a kanji, the text
// must continue with a kana that the word's inflection can produce, so "広く" never matches "広告"
// and "重い" never matches "重なる".
constexpr std::array<std::u32string_view, 10> kana_rows{U"ぁあぃいぅうぇえぉおゔ", U"かがきぎくぐけげこご", U"さざしじすずせぜそぞ",
    U"ただちぢっつづてでとど", U"なにぬねの", U"はばぱひびぴふぶぷへべぺほぼぽ", U"まみむめも", U"ゃやゅゆょよ", U"らりるれろ", U"ゎわゐゑをん"};
constexpr std::u32string_view dictionary_endings = U"うくぐすつぬぶむるい";
constexpr std::u32string_view adjective_forms = U"いくかけさめそ";  // 広い 広く 広かった 広ければ 広さ 広め 広そう
std::u32string_view row_of(char32_t c) {
    for (const auto row : kana_rows) if (row.find(c) != std::u32string_view::npos) return row;
    return {};
}
bool inflects_to(char32_t ending, char32_t next) {
    if (ending == U'い' || ending == U'く') if (adjective_forms.find(next) != std::u32string_view::npos) return true;
    if (ending == U'い') return false;
    if (row_of(ending).find(next) != std::u32string_view::npos) return true;
    // Sound changes before て / た: 押って 飛んで 書いて, and 言わない.
    if (next == U'っ') return ending == U'う' || ending == U'つ' || ending == U'る';
    if (next == U'ん') return ending == U'む' || ending == U'ぶ' || ending == U'ぬ';
    if (next == U'い') return ending == U'く' || ending == U'ぐ';
    return next == U'わ' && ending == U'う';
}
bool mentions_inflected(std::u32string_view folded, std::u32string_view needle) {
    if (needle.size() < 2 || dictionary_endings.find(needle.back()) == std::u32string_view::npos) return false;
    if (std::none_of(needle.begin(), needle.end() - 1, kanji)) return false;
    const auto stem = needle.substr(0, needle.size() - 1);
    const bool open = kanji(stem.back());
    for (auto at = folded.find(stem); at != std::u32string_view::npos; at = folded.find(stem, at + 1)) {
        if (!open) return true;
        const auto next = at + stem.size();
        if (next < folded.size() && hiragana(folded[next]) && inflects_to(needle.back(), folded[next])) return true;
    }
    return false;
}
}
bool mentions(std::u32string_view folded, std::string_view keyword) {
    const auto needle = fold(keyword);
    if (needle.empty()) return false;
    return folded.find(needle) != std::u32string_view::npos || mentions_inflected(folded, needle);
}
bool mentions_any(std::u32string_view folded, const std::vector<std::string>& keywords) {
    return std::any_of(keywords.begin(), keywords.end(), [&](const std::string& k) { return mentions(folded, k); });
}
double similarity(std::u32string_view a, std::u32string_view b) {
    const auto bigrams = [](std::u32string_view text) {
        std::set<std::uint64_t> grams;
        for (std::size_t i = 0; i + 1 < text.size(); ++i) grams.insert((static_cast<std::uint64_t>(text[i]) << 32) | text[i + 1]);
        return grams;
    };
    const auto left = bigrams(a), right = bigrams(b);
    if (left.empty() || right.empty()) return a == b && !a.empty() ? 1.0 : 0.0;
    std::size_t shared = 0;
    for (const auto gram : left) shared += right.count(gram);
    return static_cast<double>(shared) / static_cast<double>(left.size() + right.size() - shared);
}
std::size_t longest_run(std::u32string_view text) {
    std::size_t best = 0, run = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        run = i > 0 && text[i] == text[i - 1] ? run + 1 : 1;
        best = std::max(best, run);
    }
    return best;
}
std::string trim(std::string_view text) {
    constexpr std::string_view blank = " \t\r\n";
    const auto begin = text.find_first_not_of(blank);
    if (begin == std::string_view::npos) return {};
    return std::string(text.substr(begin, text.find_last_not_of(blank) - begin + 1));
}
}
