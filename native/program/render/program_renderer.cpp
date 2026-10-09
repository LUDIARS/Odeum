#include "program_renderer.hpp"
#ifdef ODEUM_PROGRAM_PICTOR_OFFSCREEN
#include "pictor_offscreen_renderer.hpp"
#endif

namespace odeum::program {
namespace {
// The boards and the not-connected plate: a dark navy and a dark grey, as Y/U/V.
constexpr std::uint8_t board_y = 32, board_u = 140, board_v = 124;
constexpr std::uint8_t plate_y = 40, plate_u = 128, plate_v = 128;
}

CpuProgramRenderer::CpuProgramRenderer(int width, int height) : frame_(width, height) {}

const Nv12Image& CpuProgramRenderer::compose(const CompositionInput& input) {
    const Rect whole{0, 0, frame_.width, frame_.height};
    if (input.layout.board != Board::none) fill(frame_, whole, board_y, board_u, board_v);
    else fill(frame_, whole, 16, 128, 128);
    for (const auto& tile : input.layout.tiles) {
        const auto& picture = input.pictures[input_at(tile.input)];
        if (tile.live && picture) draw_picture(frame_, *picture, tile.area);
        else fill(frame_, tile.area, plate_y, plate_u, plate_v);
    }
    for (const auto& layer : input.layers) {
        if (!layer.pixels || layer.pixels->width <= 0) continue;
        blend_layer(frame_, layer.pixels->pixels, layer.pixels->width, layer.pixels->height, layer.x, layer.y);
    }
    return frame_;
}

std::unique_ptr<ProgramRenderer> make_program_renderer(int width, int height) {
#ifdef ODEUM_PROGRAM_PICTOR_OFFSCREEN
    if (auto gpu = PictorOffscreenRenderer::create(width, height)) return gpu;
#endif
    return std::make_unique<CpuProgramRenderer>(width, height);
}
}
