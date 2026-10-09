#pragma once
#include "../core/program_settings.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace odeum::program {
// Reads the settings file; a missing file yields the defaults. Throws std::invalid_argument
// (with the path) for unreadable JSON or invalid values so a broken file is never overwritten.
ProgramSettings load_settings(const std::filesystem::path&);
// Writes through a temporary file and a rename, creating the folder when needed.
void save_settings(const std::filesystem::path&, const ProgramSettings&);

struct ProgramOptions {
    std::optional<std::string> launch_url; // odeum://produce?relay=...&ticket=...
    std::optional<std::string> font;
    bool help = false;
};
// odeum-program [odeum://produce?...] [--font F.ttf] [--help]. Throws std::invalid_argument for
// an unknown option or a missing value.
ProgramOptions parse_program_options(const std::vector<std::string>& arguments);
std::string program_usage();
}
