#include "nv12_canvas.hpp"
#include "core/nv12_scaler.hpp"
#include <algorithm>
#include <stdexcept>

namespace odeum::program {
namespace {
using presenter::fit_letterbox;
constexpr std::uint8_t black_y = 16, neutral = 128;

std::size_t at(int x, int y, int stride) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x); }

// BT.709 limited range, 8-bit fixed point (the same coefficients as the presenter's scaler).
int luma(int r, int g, int b) { return std::clamp((47 * r + 157 * g + 16 * b + 128) / 256 + 16, 16, 235); }
int chroma_u(int r, int g, int b) { return std::clamp((-26 * r - 87 * g + 112 * b + 128) / 256 + 128, 16, 240); }
int chroma_v(int r, int g, int b) { return std::clamp((112 * r - 102 * g - 10 * b + 128) / 256 + 128, 16, 240); }

Rect clipped(Rect area, int width, int height) {
    const int x0 = std::clamp(area.x & ~1, 0, width), y0 = std::clamp(area.y & ~1, 0, height);
    const int x1 = std::clamp((area.x + area.width) & ~1, 0, width), y1 = std::clamp((area.y + area.height) & ~1, 0, height);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

// Samples a source NV12 plane pair into a destination rectangle by nearest source box.
void resample(const std::uint8_t* src, int sw, int sh, std::uint8_t* dst, int dst_stride, int dst_height, Rect box) {
    if (box.width <= 0 || box.height <= 0) return;
    const auto* src_uv = src + static_cast<std::size_t>(sw) * static_cast<std::size_t>(sh);
    auto* dst_uv = dst + static_cast<std::size_t>(dst_stride) * static_cast<std::size_t>(dst_height);
    std::vector<int> xs(static_cast<std::size_t>(box.width));
    for (int x = 0; x < box.width; ++x) xs[static_cast<std::size_t>(x)] = static_cast<int>(static_cast<std::int64_t>(x) * sw / box.width);
    for (int y = 0; y < box.height; ++y) {
        const int sy = static_cast<int>(static_cast<std::int64_t>(y) * sh / box.height);
        const auto* row = src + at(0, sy, sw);
        auto* out = dst + at(box.x, box.y + y, dst_stride);
        for (int x = 0; x < box.width; ++x) out[x] = row[xs[static_cast<std::size_t>(x)]];
    }
    for (int y = 0; y < box.height / 2; ++y) {
        const int sy = static_cast<int>(static_cast<std::int64_t>(y) * (sh / 2) / (box.height / 2));
        const auto* row = src_uv + at(0, sy, sw);
        auto* out = dst_uv + at(box.x, box.y / 2 + y, dst_stride);
        for (int x = 0; x < box.width / 2; ++x) {
            const int sx = (xs[static_cast<std::size_t>(x * 2)] / 2) * 2;
            out[x * 2] = row[sx];
            out[x * 2 + 1] = row[sx + 1];
        }
    }
}
}

Nv12Image::Nv12Image(int w, int h) : width(w), height(h) {
    if (w <= 0 || h <= 0 || w % 2 || h % 2) throw std::invalid_argument("NV12 needs even positive dimensions");
    const auto luma_size = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    pixels.assign(luma_size + luma_size / 2, neutral);
    std::fill(pixels.begin(), pixels.begin() + static_cast<std::ptrdiff_t>(luma_size), black_y);
}

void fill(Nv12Image& image, Rect area, std::uint8_t y, std::uint8_t u, std::uint8_t v) {
    const auto box = clipped(area, image.width, image.height);
    auto* uv = image.pixels.data() + static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    for (int row = box.y; row < box.y + box.height; ++row)
        std::fill_n(image.pixels.data() + at(box.x, row, image.width), box.width, y);
    for (int row = box.y / 2; row < (box.y + box.height) / 2; ++row) {
        auto* out = uv + at(box.x, row, image.width);
        for (int x = 0; x < box.width; x += 2) { out[x] = u; out[x + 1] = v; }
    }
}

void draw_picture(Nv12Image& image, const Nv12Picture& picture, Rect area) {
    const auto box = clipped(area, image.width, image.height);
    fill(image, box, black_y, neutral, neutral);
    if (!picture.pixels || picture.width < 2 || picture.height < 2 || box.width < 2 || box.height < 2) return;
    if (picture.pixels->size() < static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height) * 3 / 2) return;
    const auto fit = fit_letterbox(picture.width, picture.height, box.width, box.height);
    resample(picture.pixels->data(), picture.width, picture.height, image.pixels.data(), image.width, image.height,
             {box.x + fit.x, box.y + fit.y, fit.width, fit.height});
}

void blend_layer(Nv12Image& image, std::span<const unsigned char> bgra, int width, int height, int left, int top) {
    if (bgra.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) throw std::invalid_argument("BGRA layer too small");
    auto* uv_plane = image.pixels.data() + static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    const int x0 = std::max(0, left) & ~1, y0 = std::max(0, top) & ~1;
    const int x1 = std::min(image.width, left + width), y1 = std::min(image.height, top + height);
    for (int by = y0; by < y1; by += 2) {
        for (int bx = x0; bx < x1; bx += 2) {
            int coverage = 0, r_sum = 0, g_sum = 0, b_sum = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const int x = bx + dx, y = by + dy, lx = x - left, ly = y - top;
                    if (x >= image.width || y >= image.height || lx < 0 || ly < 0 || lx >= width || ly >= height) continue;
                    const auto* p = bgra.data() + (static_cast<std::size_t>(ly) * static_cast<std::size_t>(width) + static_cast<std::size_t>(lx)) * 4;
                    const int a = p[3];
                    if (a == 0) continue;
                    // Premultiplied over: Y = K*Cp + 16 + (Ydst - 16)(1 - a) = (luma(Cp) - 16 + 16a) + Ydst(1 - a).
                    auto& yv = image.pixels[at(x, y, image.width)];
                    const int src_y = luma(p[2], p[1], p[0]) - 16 + 16 * a / 255;
                    yv = static_cast<std::uint8_t>(std::clamp(src_y + yv * (255 - a) / 255, 16, 235));
                    coverage += a; r_sum += p[2]; g_sum += p[1]; b_sum += p[0];
                }
            }
            if (coverage == 0) continue;
            auto* uv = uv_plane + at(bx, by / 2, image.width);
            const int a = coverage / 4;
            const int su = chroma_u(r_sum / 4, g_sum / 4, b_sum / 4) - 128 + 128 * a / 255;
            const int sv = chroma_v(r_sum / 4, g_sum / 4, b_sum / 4) - 128 + 128 * a / 255;
            uv[0] = static_cast<std::uint8_t>(std::clamp(su + uv[0] * (255 - a) / 255, 16, 240));
            uv[1] = static_cast<std::uint8_t>(std::clamp(sv + uv[1] * (255 - a) / 255, 16, 240));
        }
    }
}

void scale_into(const Nv12Image& source, Nv12Image& target) {
    std::fill(target.pixels.begin(), target.pixels.begin() + static_cast<std::ptrdiff_t>(target.width) * target.height, black_y);
    std::fill(target.pixels.begin() + static_cast<std::ptrdiff_t>(target.width) * target.height, target.pixels.end(), neutral);
    const auto fit = fit_letterbox(source.width, source.height, target.width, target.height);
    resample(source.pixels.data(), source.width, source.height, target.pixels.data(), target.width, target.height, {fit.x, fit.y, fit.width, fit.height});
}

std::vector<unsigned char> thumbnail_bgra(const Nv12Picture& picture, int width, int height) {
    std::vector<unsigned char> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    if (!picture.pixels || picture.width < 2 || picture.height < 2) return out;
    const auto* y_plane = picture.pixels->data();
    const auto* uv_plane = y_plane + static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height);
    for (int y = 0; y < height; ++y) {
        const int sy = static_cast<int>(static_cast<std::int64_t>(y) * picture.height / height);
        for (int x = 0; x < width; ++x) {
            const int sx = static_cast<int>(static_cast<std::int64_t>(x) * picture.width / width);
            const int c = (y_plane[at(sx, sy, picture.width)] - 16) * 298;
            const auto* uv = uv_plane + at(sx & ~1, sy / 2, picture.width);
            const int d = uv[0] - 128, e = uv[1] - 128;
            auto* p = out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
            // BT.709 limited range to RGB, 8-bit fixed point.
            p[2] = static_cast<unsigned char>(std::clamp((c + 459 * e + 128) >> 8, 0, 255));
            p[1] = static_cast<unsigned char>(std::clamp((c - 55 * d - 136 * e + 128) >> 8, 0, 255));
            p[0] = static_cast<unsigned char>(std::clamp((c + 541 * d + 128) >> 8, 0, 255));
            p[3] = 255;
        }
    }
    return out;
}
}
