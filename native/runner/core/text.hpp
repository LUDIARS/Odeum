#pragma once
// JavaScript string semantics needed to keep the former validation results: lengths in UTF-16
// code units or code points, String#trim and the \s class. Input is valid UTF-8 (JSON-parsed).
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace odeum::runner {
std::vector<char32_t> code_points(std::string_view utf8);
std::string encode_utf8(const std::vector<char32_t>& points);
std::size_t utf16_length(std::string_view utf8);
std::size_t code_point_length(std::string_view utf8);
bool is_js_whitespace(char32_t c);  // \s and String#trim
std::string js_trim(std::string_view utf8);
// The first `units` UTF-16 code units, without splitting a surrogate pair.
std::string utf16_prefix(std::string_view utf8, std::size_t units);
}
