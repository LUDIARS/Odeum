#pragma once
#include "presenter_settings.hpp"
#include <optional>
#include <string>
#include <vector>

namespace odeum::presenter {
enum class StartupAction { run, register_url_scheme, unregister_url_scheme, help };

struct PresenterOptions {
    StartupAction action = StartupAction::run;
    PresenterSettings settings;
    std::optional<std::string> launch_url; // odeum://present?... given on the command line
};

// Reads the command line on top of the stored settings:
//   odeum-presenter [odeum://present?...] [--font F.ttf] [--width W --height H] [--fps N]
//                   [--max-bitrate-kbps N] [--audio | --no-audio] [--overlay-corner C]
//                   [--register-url-scheme | --unregister-url-scheme | --help]
// The launch link is checked later by parse_launch_url so a bad link reaches the panel as a
// message instead of aborting. macOS's -psn_* process serial argument is ignored. Throws
// std::invalid_argument for an unknown option, a missing value or an out-of-range number.
PresenterOptions parse_options(const std::vector<std::string>& arguments, PresenterSettings stored);

std::string usage();
}
