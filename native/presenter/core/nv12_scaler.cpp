#include "nv12_scaler.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace odeum::presenter {
namespace {
struct Rgb { int r, g, b; };
// BT.709 limited range, 8-bit fixed point.
std::uint8_t luma(Rgb c) { return static_cast<std::uint8_t>(std::clamp((47 * c.r + 157 * c.g + 16 * c.b + 128) / 256 + 16, 16, 235)); }
std::uint8_t chroma_u(Rgb c) { return static_cast<std::uint8_t>(std::clamp((-26 * c.r - 87 * c.g + 112 * c.b + 128) / 256 + 128, 16, 240)); }
std::uint8_t chroma_v(Rgb c) { return static_cast<std::uint8_t>(std::clamp((112 * c.r - 102 * c.g - 10 * c.b + 128) / 256 + 128, 16, 240)); }
}

Letterbox fit_letterbox(int sw, int sh, int ow, int oh) {
    if (sw <= 0 || sh <= 0 || ow <= 0 || oh <= 0) throw std::invalid_argument("Sizes must be positive");
    int w = ow, h = static_cast<int>(static_cast<std::int64_t>(ow) * sh / sw);
    if (h > oh) { h = oh; w = static_cast<int>(static_cast<std::int64_t>(oh) * sw / sh); }
    w = std::max(2, w & ~1);
    h = std::max(2, h & ~1);
    return {((ow - w) / 2) & ~1, ((oh - h) / 2) & ~1, w, h};
}

void bgra_to_nv12(std::span<const std::uint8_t> bgra, int width, int height, int stride,
                  std::span<std::uint8_t> nv12, int ow, int oh) {
    if (ow <= 0 || oh <= 0 || ow % 2 || oh % 2) throw std::invalid_argument("NV12 needs even positive dimensions");
    if (width <= 0 || height <= 0 || stride < width * 4 || bgra.size() < static_cast<std::size_t>(stride) * static_cast<std::size_t>(height - 1) + static_cast<std::size_t>(width) * 4)
        throw std::invalid_argument("BGRA buffer too small");
    const auto y_size = static_cast<std::size_t>(ow) * static_cast<std::size_t>(oh);
    if (nv12.size() < y_size + y_size / 2) throw std::invalid_argument("NV12 buffer too small");
    std::fill(nv12.begin(), nv12.begin() + static_cast<std::ptrdiff_t>(y_size), std::uint8_t{16});
    std::fill(nv12.begin() + static_cast<std::ptrdiff_t>(y_size), nv12.begin() + static_cast<std::ptrdiff_t>(y_size + y_size / 2), std::uint8_t{128});
    const auto box = fit_letterbox(width, height, ow, oh);
    // Source span covered by each output column/row; at least one pixel so enlarging works too.
    std::vector<int> x0(static_cast<std::size_t>(box.width) + 1), y0(static_cast<std::size_t>(box.height) + 1);
    for (int i = 0; i <= box.width; ++i) x0[static_cast<std::size_t>(i)] = static_cast<int>(static_cast<std::int64_t>(i) * width / box.width);
    for (int i = 0; i <= box.height; ++i) y0[static_cast<std::size_t>(i)] = static_cast<int>(static_cast<std::int64_t>(i) * height / box.height);
    const auto pixel = [&](int x, int y) -> Rgb {
        int r = 0, g = 0, b = 0, n = 0;
        const int xe = std::max(x0[static_cast<std::size_t>(x)] + 1, x0[static_cast<std::size_t>(x) + 1]);
        const int ye = std::max(y0[static_cast<std::size_t>(y)] + 1, y0[static_cast<std::size_t>(y) + 1]);
        // Larger shrink factors sample a 2x2 lattice of the box; enough for screen text, much cheaper.
        const int xs = std::max(1, (xe - x0[static_cast<std::size_t>(x)]) / 2), ys = std::max(1, (ye - y0[static_cast<std::size_t>(y)]) / 2);
        for (int sy = y0[static_cast<std::size_t>(y)]; sy < std::min(ye, height); sy += ys) {
            const auto* row = bgra.data() + static_cast<std::size_t>(sy) * static_cast<std::size_t>(stride);
            for (int sx = x0[static_cast<std::size_t>(x)]; sx < std::min(xe, width); sx += xs) {
                const auto* p = row + static_cast<std::size_t>(sx) * 4;
                b += p[0]; g += p[1]; r += p[2]; ++n;
            }
        }
        return {r / n, g / n, b / n};
    };
    auto* y_plane = nv12.data();
    auto* uv_plane = nv12.data() + y_size;
    for (int y = 0; y < box.height; y += 2) {
        for (int x = 0; x < box.width; x += 2) {
            const Rgb c[4] = {pixel(x, y), pixel(x + 1, y), pixel(x, y + 1), pixel(x + 1, y + 1)};
            const auto ox = static_cast<std::size_t>(box.x + x), oy = static_cast<std::size_t>(box.y + y);
            y_plane[oy * static_cast<std::size_t>(ow) + ox] = luma(c[0]);
            y_plane[oy * static_cast<std::size_t>(ow) + ox + 1] = luma(c[1]);
            y_plane[(oy + 1) * static_cast<std::size_t>(ow) + ox] = luma(c[2]);
            y_plane[(oy + 1) * static_cast<std::size_t>(ow) + ox + 1] = luma(c[3]);
            const Rgb average{(c[0].r + c[1].r + c[2].r + c[3].r) / 4, (c[0].g + c[1].g + c[2].g + c[3].g) / 4, (c[0].b + c[1].b + c[2].b + c[3].b) / 4};
            auto* uv = uv_plane + (oy / 2) * static_cast<std::size_t>(ow) + ox;
            uv[0] = chroma_u(average);
            uv[1] = chroma_v(average);
        }
    }
}
}
