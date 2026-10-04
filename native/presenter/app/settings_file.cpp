#include "settings_file.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace odeum::presenter {
PresenterSettings load_settings(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::stringstream text;
    text << input.rdbuf();
    const auto json = Json::parse(text.str(), nullptr, false);
    if (json.is_discarded()) throw std::invalid_argument("Settings file is not JSON: " + path.string());
    try { return settings_from_json(json); }
    catch (const std::invalid_argument& error) { throw std::invalid_argument(std::string(error.what()) + " (" + path.string() + ")"); }
}

void save_settings(const std::filesystem::path& path, const PresenterSettings& settings) {
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

std::filesystem::path resolve_font(const PresenterSettings& settings, const std::vector<std::filesystem::path>& candidates) {
    if (!settings.font.empty()) {
        const std::filesystem::path font(std::u8string(settings.font.begin(), settings.font.end()));
        if (!std::filesystem::is_regular_file(font)) throw std::runtime_error("Font not found: " + settings.font);
        return font;
    }
    for (const auto& candidate : candidates) if (std::filesystem::is_regular_file(candidate)) return candidate;
    throw std::runtime_error("No TrueType font found; pass --font <file.ttf> (a .ttf with Japanese glyphs)");
}
}
