#pragma once
#include "../core/presenter_settings.hpp"
#include <tela/desktop_placement.hpp>

namespace odeum::presenter {
// Settings <-> Tela, with the overlay's fixed size filled in.
tela::DesktopPlacement to_tela(const OverlayPlacement&, float width, float height);
OverlayPlacement from_tela(const tela::DesktopPlacement&);
}
