#pragma once
#include "program_controller.hpp"
#include <tela/document.hpp>

namespace odeum::program {
inline constexpr float program_panel_width = 720, program_panel_height = 760;
// On macOS the panel is a desktop surface (Tela has no AppKit window host); this is its grip.
inline constexpr const char* program_panel_grip = "panel-grip";

// The operator panel: input thumbnails and connection state, full screen 1..4 / quad / standby /
// end, per-input mute, programme volume, YouTube start/stop and stream key, return feed
// start/stop, and the publish state (bitrate, drops, reconnections).
tela::Document program_panel_document(ProgramController& controller, bool with_grip);
tela::Theme program_panel_theme();
}
