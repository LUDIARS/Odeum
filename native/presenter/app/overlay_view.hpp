#pragma once
#include "../core/overlay_state.hpp"
#include <tela/document.hpp>

namespace odeum::presenter {
inline constexpr float overlay_width = 340, overlay_height = 560;
// Exclusive region that drags the overlay (DesktopOverlayOptions::grip).
inline constexpr const char* overlay_grip = "grip";

struct OverlayViewInput {
    const OverlayState& state;
    Millis now{};
    bool comments_visible{};
};

// The desktop overlay's declaration: good fountain and total, named stamps, poll tally, viewer
// count and (when enabled) the comment stream. Only the grip takes input; the rest passes
// clicks through to the presentation underneath.
tela::Document overlay_document(const OverlayViewInput& input);
tela::Theme overlay_theme();
}
