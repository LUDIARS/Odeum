#pragma once
#include "program_renderer.hpp"
#include "core/overlay_state.hpp"
#include <tela/pictor_surface.hpp>
#include <tela/runtime.hpp>

namespace odeum::program {
// Draws the text layers of the programme with Tela on Pictor's TrueType rasteriser: the boards
// and "input not connected" plates of the current layout, and the audience's reactions (the
// presenter's overlay: good fountain, stamps, public telops and submissions, poll, viewers) at
// the bottom-right. UI thread only.
class LayerRenderer {
public:
    LayerRenderer(const std::string& font, int width, int height);
    // Re-rendered only when the layout changes.
    Layer plates(const ProgramLayout& layout);
    Layer reactions(const presenter::OverlayState& state, presenter::Millis now);
private:
    int width_, height_;
    tela::PictorSurface surface_;
    tela::Runtime plate_runtime_, reaction_runtime_;
    std::optional<ProgramLayout> plate_layout_;
    Layer plate_layer_;
};

// The plate and board text for a layout, in programme pixels (transparent elsewhere).
tela::Document plate_document(const ProgramLayout& layout);
}
