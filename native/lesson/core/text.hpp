#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace odeum::lesson {
// Text helpers for the provisional (rule-based) lesson model and for duplicate detection.
// Every function throws ProtocolError("invalid_message") on malformed UTF-8.
std::u32string decode_utf8(std::string_view text);
std::string encode_utf8(std::u32string_view text);
// Comparison form: ASCII lowercased, fullwidth ASCII narrowed, halfwidth katakana widened (with its
// voiced marks), katakana folded to hiragana, whitespace and punctuation removed.
// "ヒット ストップ！", "ﾋｯﾄｽﾄｯﾌﾟ" and "ひっとすとっぷ" fold to the same string.
std::u32string fold(std::string_view text);
// True when the folded text contains the keyword. A keyword in dictionary form ("見える", "広く",
// "下がる") also matches its inflections ("見えない", "広めに", "下がった"); see text.cpp.
bool mentions(std::u32string_view folded, std::string_view keyword);
bool mentions_any(std::u32string_view folded, const std::vector<std::string>& keywords);
// Jaccard similarity of character bigrams over the folded text, 0..1.
double similarity(std::u32string_view folded_a, std::u32string_view folded_b);
// Length of the longest run of one repeated scalar ("wwwwww" -> 6).
std::size_t longest_run(std::u32string_view text);
std::string trim(std::string_view text);
}
