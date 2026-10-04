#include "placement_mapping.hpp"

namespace odeum::presenter {
tela::DesktopPlacement to_tela(const OverlayPlacement& p, float width, float height) {
    tela::DesktopPlacement placement;
    placement.width = width;
    placement.height = height;
    placement.corner = tela::desktop_corner_from_name(p.corner);
    placement.margin_x = p.margin_x;
    placement.margin_y = p.margin_y;
    placement.absolute = p.absolute;
    placement.x = p.x;
    placement.y = p.y;
    return placement;
}

OverlayPlacement from_tela(const tela::DesktopPlacement& p) {
    return {tela::desktop_corner_name(p.corner), p.margin_x, p.margin_y, p.absolute, p.x, p.y};
}
}
