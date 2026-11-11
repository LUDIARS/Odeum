#include "catalog.hpp"
#include "assets.hpp"
#include <fstream>
#include <sstream>

namespace odeum::lesson {
namespace {
constexpr std::size_t max_plot_bytes = 1024 * 1024;
Plot parse(std::string_view text) {
    Json document;
    try { document = Json::parse(text); } catch (const Json::exception&) { throw ProtocolError("invalid_plot", "Plot is not valid JSON"); }
    return parse_plot(document);
}
}
std::vector<Plot> builtin_plots() {
    std::vector<Plot> plots;
    for (const auto path : asset_paths("/courses/")) plots.push_back(parse(find_asset(path)->body));
    return plots;
}
Plot load_plot_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read plot file");
    std::ostringstream buffer; buffer << file.rdbuf();
    const auto text = buffer.str();
    if (text.size() > max_plot_bytes) throw ProtocolError("invalid_plot", "Plot file is too large");
    return parse(text);
}
}
