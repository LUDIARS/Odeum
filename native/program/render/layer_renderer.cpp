#include "layer_renderer.hpp"
#include "../core/program_settings.hpp"
#include "app/overlay_view.hpp"

namespace odeum::program {
namespace {
// The reaction overlay is declared at 340x560 logical pixels; on a 1080p programme it is drawn
// half as large again and kept clear of the edges.
constexpr float reaction_scale = 1.5f;
constexpr int reaction_margin = 32;

tela::Layout centred(Rect area, float height, float scale) {
    tela::Layout layout;
    layout.positioned = true;
    layout.x = static_cast<float>(area.x) + static_cast<float>(area.width) * 0.1f;
    layout.y = static_cast<float>(area.y) + (static_cast<float>(area.height) - height) / 2;
    layout.width = static_cast<float>(area.width) * 0.8f;
    layout.height = height;
    layout.text_scale = scale;
    return layout;
}

tela::Viewport viewport(const char* id, int width, int height, float scale) {
    tela::Viewport view;
    view.host_id = "odeum-program";
    view.view_id = id;
    view.width = width;
    view.height = height;
    view.dpi_scale = scale;
    view.visible = true;
    return view;
}

tela::Theme plate_theme() {
    tela::Theme theme;
    theme.panel = {0, 0, 0, 0};
    theme.text = {236, 238, 245, 255};
    theme.font_size = 24;
    theme.line_height = 34;
    return theme;
}
}

tela::Document plate_document(const ProgramLayout& layout) {
    tela::Document doc;
    const Rect whole{0, 0, program_width, program_height};
    if (layout.board == Board::standby) doc.text("board", "まもなく始まります", centred(whole, 120, 2.6f));
    if (layout.board == Board::ended) doc.text("board", "ご視聴ありがとうございました", centred(whole, 120, 2.6f));
    for (const auto& tile : layout.tiles) {
        if (tile.live) continue;
        const float scale = tile.area.width >= program_width ? 2.f : 1.3f;
        doc.text("plate-" + std::to_string(tile.input), "入力 " + std::to_string(tile.input + 1) + " 未接続", centred(tile.area, 80 * scale / 2, scale));
    }
    return doc;
}

LayerRenderer::LayerRenderer(const std::string& font, int width, int height) : width_(width), height_(height), surface_(font) {
    plate_runtime_.theme(plate_theme());
    plate_runtime_.viewport(viewport("plates", width, height, 1.f));
    reaction_runtime_.theme(presenter::overlay_theme());
    reaction_runtime_.viewport(viewport("reactions", static_cast<int>(presenter::overlay_width * reaction_scale),
                                        static_cast<int>(presenter::overlay_height * reaction_scale), reaction_scale));
}

Layer LayerRenderer::plates(const ProgramLayout& layout) {
    if (plate_layout_ && *plate_layout_ == layout) return plate_layer_;
    plate_layout_ = layout;
    plate_runtime_.document(plate_document(layout));
    plate_layer_ = {std::make_shared<const tela::PixelSurface>(surface_.render(plate_runtime_)), 0, 0};
    return plate_layer_;
}

Layer LayerRenderer::reactions(const presenter::OverlayState& state, presenter::Millis now) {
    reaction_runtime_.document(presenter::overlay_document({state, now, true}));
    auto pixels = std::make_shared<const tela::PixelSurface>(surface_.render(reaction_runtime_));
    const int x = width_ - pixels->width - reaction_margin, y = height_ - pixels->height - reaction_margin;
    return {std::move(pixels), x, y};
}
}
