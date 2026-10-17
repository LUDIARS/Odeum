#pragma once
#include <algorithm>
#include <stdexcept>

namespace odeum::presenter {
// Percentages of the selected display, independent of DPI and desktop origin.
struct CaptureRegion {
    int x = 0, y = 0, width = 100, height = 100;
    bool operator==(const CaptureRegion&) const = default;
};
struct CapturePixels { int x, y, width, height; };
inline CapturePixels capture_pixels(const CaptureRegion& r, int width, int height) {
    if (r.x < 0 || r.y < 0 || r.width < 5 || r.height < 5 || r.width > 100 || r.height > 100 ||
        r.x > 100 - r.width || r.y > 100 - r.height || width < 2 || height < 2)
        throw std::invalid_argument("Capture region is outside the selected display");
    const int x = width * r.x / 100, y = height * r.y / 100;
    return {x, y, std::max(1, width * (r.x + r.width) / 100 - x),
                  std::max(1, height * (r.y + r.height) / 100 - y)};
}
}
