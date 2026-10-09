#pragma once
#include "nv12_canvas.hpp"
#include <tela/pixel_surface.hpp>
#include <array>
#include <memory>
#include <optional>

namespace odeum::program {
// A premultiplied BGRA layer drawn over the inputs at (x, y) in programme pixels.
struct Layer {
    std::shared_ptr<const tela::PixelSurface> pixels;
    int x{}, y{};
};

struct CompositionInput {
    ProgramLayout layout;
    // The newest decoded picture of each input. Only tiles marked live draw theirs; a tile whose
    // input dropped shows the plate, never this (possibly stale) picture.
    std::array<std::optional<Nv12Picture>, input_count> pictures;
    // Boards, plates and the reaction layer, drawn in order.
    std::vector<Layer> layers;
};

// Composes one programme frame (1920x1080 NV12). Two implementations share this interface:
// CpuProgramRenderer (in memory, available today) and the Pictor GPU renderer, which needs
// Pictor's offscreen render target + NV12 readback (pictor_offscreen_renderer.hpp; built only
// with ODEUM_PROGRAM_PICTOR_OFFSCREEN once Pictor provides that API).
class ProgramRenderer {
public:
    virtual ~ProgramRenderer() = default;
    // The returned image stays valid until the next compose().
    virtual const Nv12Image& compose(const CompositionInput& input) = 0;
};

class CpuProgramRenderer final : public ProgramRenderer {
public:
    CpuProgramRenderer(int width, int height);
    const Nv12Image& compose(const CompositionInput& input) override;
private:
    Nv12Image frame_;
};

// The renderer this build has: Pictor's GPU path when compiled in and a device is available,
// else the CPU one.
std::unique_ptr<ProgramRenderer> make_program_renderer(int width, int height);
}
