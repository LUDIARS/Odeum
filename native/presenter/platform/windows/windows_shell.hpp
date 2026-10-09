#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace odeum::presenter::windows {
// Win32 shell helpers shared by odeum-presenter and odeum-program.
// Plain text on the clipboard as UTF-8; empty when there is none.
std::string clipboard_text();
// %APPDATA% (roaming) and %LOCALAPPDATA%; throws std::runtime_error when unavailable.
std::filesystem::path roaming_app_data();
std::filesystem::path local_app_data();
// Japanese faces Windows ships as single .ttf files (Pictor cannot read .ttc collections).
std::vector<std::filesystem::path> system_font_candidates();
}
