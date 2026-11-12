#include "catalog.hpp"
#include "assets.hpp"
#include "input_limits.hpp"
#include <odeum/bounded_file.hpp>

namespace odeum::lesson {
namespace {
Plot parse(std::string_view text) { return parse_plot(parse_plot_text(text)); }
}
std::vector<Plot> builtin_plots() {
    std::vector<Plot> plots;
    for (const auto path : asset_paths("/courses/")) plots.push_back(parse(find_asset(path)->body));
    return plots;
}
Plot load_plot_file(const std::string& path) {
    // Read in chunks up to the limit + 1 byte, so a huge file is refused without being kept whole.
    std::optional<std::string> text;
    try { text = read_file_within(path, plot_file_limits.bytes); }
    catch (const FileTooLarge&) { throw ProtocolError("invalid_plot", "Plot file is too large"); }
    if (!text) throw std::runtime_error("Cannot read plot file");
    return parse(*text);
}
}
