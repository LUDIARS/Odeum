#include "presenter_options.hpp"
#include <charconv>
#include <stdexcept>

namespace odeum::presenter {
namespace {
int number(const std::string& option, const std::string& text) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) throw std::invalid_argument(option + " needs a whole number");
    return value;
}
}

PresenterOptions parse_options(const std::vector<std::string>& arguments, PresenterSettings stored) {
    PresenterOptions options;
    options.settings = std::move(stored);
    auto& s = options.settings;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        const auto value = [&]() -> const std::string& {
            if (i + 1 >= arguments.size()) throw std::invalid_argument(argument + " needs a value");
            return arguments[++i];
        };
        if (argument.starts_with("-psn_")) continue;
        if (argument == "--help" || argument == "-h") options.action = StartupAction::help;
        else if (argument == "--register-url-scheme") options.action = StartupAction::register_url_scheme;
        else if (argument == "--unregister-url-scheme") options.action = StartupAction::unregister_url_scheme;
        else if (argument == "--font") s.font = value();
        else if (argument == "--width") s.stream.width = number(argument, value());
        else if (argument == "--height") s.stream.height = number(argument, value());
        else if (argument == "--fps") s.stream.fps = number(argument, value());
        else if (argument == "--max-bitrate-kbps") s.stream.max_bitrate_kbps = number(argument, value());
        else if (argument == "--audio") s.stream.audio = true;
        else if (argument == "--no-audio") s.stream.audio = false;
        else if (argument == "--overlay-corner") {
            const auto& corner = value();
            if (!is_corner_name(corner)) throw std::invalid_argument("--overlay-corner takes top-left, top-right, bottom-left or bottom-right");
            s.overlay.corner = corner;
            s.overlay.absolute = false;
        } else if (!argument.starts_with("-") && !options.launch_url) options.launch_url = argument;
        else throw std::invalid_argument("Unknown option: " + argument);
    }
    validate(s.stream);
    return options;
}

std::string usage() {
    return "odeum-presenter [odeum://present?relay=<wss URL>&ticket=<JWS>]\n"
           "  --font <file.ttf>          TrueType font for the overlay and panel\n"
           "  --width W --height H       stream size (default 1920x1080)\n"
           "  --fps N                    frame rate 1..60 (default 30)\n"
           "  --max-bitrate-kbps N       bitrate ceiling 300..20000 (default 6000)\n"
           "  --audio | --no-audio       send the system sound as Opus (default off)\n"
           "  --overlay-corner C         top-left | top-right | bottom-left | bottom-right\n"
           "  --register-url-scheme      Windows: register odeum:// for the current user\n"
           "  --unregister-url-scheme    Windows: remove that registration\n";
}
}
