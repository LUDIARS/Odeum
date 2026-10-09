#include "program_settings_file.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace odeum::program {
ProgramSettings load_settings(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::stringstream text;
    text << input.rdbuf();
    const auto json = Json::parse(text.str(), nullptr, false);
    if (json.is_discarded()) throw std::invalid_argument("Settings file is not JSON: " + path.string());
    try { return settings_from_json(json); }
    catch (const std::invalid_argument& error) { throw std::invalid_argument(std::string(error.what()) + " (" + path.string() + ")"); }
}

void save_settings(const std::filesystem::path& path, const ProgramSettings& settings) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write " + temporary.string());
        output << settings_to_json(settings).dump(2) << '\n';
        if (!output) throw std::runtime_error("Cannot write " + temporary.string());
    }
    std::filesystem::rename(temporary, path);
}

ProgramOptions parse_program_options(const std::vector<std::string>& arguments) {
    ProgramOptions options;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if (argument == "--help" || argument == "-h") options.help = true;
        else if (argument == "--font") {
            if (i + 1 >= arguments.size()) throw std::invalid_argument("--font needs a .ttf path");
            options.font = arguments[++i];
        } else if (argument.rfind("odeum://", 0) == 0 || argument.rfind("ODEUM://", 0) == 0) options.launch_url = argument;
        else throw std::invalid_argument("Unknown option: " + argument);
    }
    return options;
}

std::string program_usage() {
    return "odeum-program [odeum://produce?relay=<wss URL>&ticket=<JWS>] [--font F.ttf] [--help]\n"
           "番組リンクは GLab の番組セッションから発行します。操作パネルの「番組リンクを貼り付け」でも受け付けます。";
}
}
