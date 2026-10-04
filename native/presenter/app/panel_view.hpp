#pragma once
#include "presenter_controller.hpp"
#include <tela/document.hpp>

namespace odeum::presenter {
inline constexpr float panel_width = 460, panel_height = 760;
// On macOS the panel is a desktop surface too (Tela has no AppKit window host); this is its grip.
inline constexpr const char* panel_grip = "panel-grip";

// The control panel: launch link, start/stop, capture target, poll editor, overlay options and
// connection state. Buttons call straight into the controller on the UI thread.
tela::Document panel_document(PresenterController& controller, bool with_grip);
tela::Theme panel_theme();
}
