#pragma once
#include "stream_settings.hpp"
#include <odeum/message.hpp>
#include <string>

namespace odeum::presenter {
// Where the overlay sits. Mirrors tela::DesktopPlacement without depending on Tela, so the
// settings file stays readable by Core and its tests.
struct OverlayPlacement {
    std::string corner = "bottom-right"; // top-left | top-right | bottom-left | bottom-right
    float margin_x = 24, margin_y = 24;
    bool absolute = false;
    int x = 0, y = 0;
    bool operator==(const OverlayPlacement&) const = default;
};

// Everything the presenter remembers between runs (the settings file) plus defaults.
struct PresenterSettings {
    StreamSettings stream;
    std::string font; // TrueType outlines (.ttf); collections (.ttc) are not supported by Pictor
    OverlayPlacement overlay;
    bool comments_visible = true;
    bool operator==(const PresenterSettings&) const = default;
};

bool is_corner_name(std::string_view);
// Reads the settings file's JSON. Missing keys keep their defaults; a wrong type or an invalid
// value throws std::invalid_argument naming the key.
PresenterSettings settings_from_json(const Json&);
Json settings_to_json(const PresenterSettings&);
}
