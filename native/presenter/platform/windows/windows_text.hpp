#pragma once
#include <string>
#include <string_view>

namespace odeum::presenter::windows {
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);
}
