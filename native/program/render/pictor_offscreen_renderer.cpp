// Built only with ODEUM_PROGRAM_PICTOR_OFFSCREEN: written against the offscreen API proposed for
// Pictor (pictor/surface/offscreen_target.h), which current Pictor does not provide.
#include "pictor_offscreen_renderer.hpp"
#include <pictor/surface/offscreen_target.h>

namespace odeum::program {
PictorOffscreenRenderer::PictorOffscreenRenderer(std::unique_ptr<pictor::OffscreenTarget> target, int width, int height)
    : target_(std::move(target)), frame_(width, height) {}

PictorOffscreenRenderer::~PictorOffscreenRenderer() = default;

std::unique_ptr<PictorOffscreenRenderer> PictorOffscreenRenderer::create(int width, int height) {
    pictor::OffscreenTargetDesc desc;
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    auto target = pictor::OffscreenTarget::create(desc);
    if (!target) return nullptr;
    return std::unique_ptr<PictorOffscreenRenderer>(new PictorOffscreenRenderer(std::move(target), width, height));
}

const Nv12Image& PictorOffscreenRenderer::compose(const CompositionInput& input) {
    auto& gpu = *target_;
    gpu.begin_frame(input.layout.board != Board::none ? pictor::OffscreenColor{0.11f, 0.12f, 0.22f, 1.f}
                                                      : pictor::OffscreenColor{0.f, 0.f, 0.f, 1.f});
    for (const auto& tile : input.layout.tiles) {
        const pictor::OffscreenRect area{tile.area.x, tile.area.y, tile.area.width, tile.area.height};
        const auto& picture = input.pictures[input_at(tile.input)];
        if (tile.live && picture && picture->pixels) {
            const auto* y = picture->pixels->data();
            const auto* uv = y + static_cast<std::size_t>(picture->width) * static_cast<std::size_t>(picture->height);
            const auto texture = gpu.upload_nv12(static_cast<uint32_t>(tile.input), picture->width, picture->height, y, uv);
            gpu.draw_texture_fit(texture, area);
        } else {
            gpu.fill(area, {0.15f, 0.15f, 0.15f, 1.f});
        }
    }
    for (const auto& layer : input.layers) {
        if (!layer.pixels || layer.pixels->width <= 0) continue;
        gpu.draw_bgra_premultiplied(layer.pixels->pixels.data(), layer.pixels->width, layer.pixels->height,
                                    {layer.x, layer.y, layer.pixels->width, layer.pixels->height});
    }
    gpu.end_frame();
    gpu.read_back_nv12(frame_.pixels.data(), frame_.pixels.size());
    return frame_;
}
}
