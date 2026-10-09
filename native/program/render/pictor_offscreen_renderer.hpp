#pragma once
#include "program_renderer.hpp"

namespace pictor { class OffscreenTarget; }

namespace odeum::program {
// The GPU path: inputs uploaded as NV12 textures, drawn into a 1920x1080 offscreen render target
// together with the BGRA layers, then read back as NV12. It depends on an API Pictor does not
// have yet (offscreen target + texture upload + NV12 readback, proposed in a separate Pictor PR;
// see spec/feature/program-production.md "Pictor のオフスクリーン経路"), so it is compiled only
// with -DODEUM_PROGRAM_PICTOR_OFFSCREEN=ON. Until then make_program_renderer() uses the CPU path.
class PictorOffscreenRenderer final : public ProgramRenderer {
public:
    // nullptr when no GPU device can be opened headless.
    static std::unique_ptr<PictorOffscreenRenderer> create(int width, int height);
    ~PictorOffscreenRenderer() override;
    const Nv12Image& compose(const CompositionInput& input) override;
private:
    PictorOffscreenRenderer(std::unique_ptr<pictor::OffscreenTarget> target, int width, int height);
    std::unique_ptr<pictor::OffscreenTarget> target_;
    Nv12Image frame_;
};
}
