#pragma once
#include "../core/layout.hpp"
#include "../core/media_codecs.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace odeum::program {
// A programme-sized NV12 picture (BT.709 limited range) being composed in memory.
struct Nv12Image {
    int width{}, height{};
    std::vector<std::uint8_t> pixels; // Y plane, then interleaved UV

    Nv12Image() = default;
    Nv12Image(int width, int height);
};

// Paints `area` (even-aligned) with one colour given as BT.709 limited-range Y/U/V.
void fill(Nv12Image& image, Rect area, std::uint8_t y, std::uint8_t u, std::uint8_t v);

// Scales `picture` into `area` keeping its aspect ratio (black bars), sampling each output
// pixel from the source box it covers.
void draw_picture(Nv12Image& image, const Nv12Picture& picture, Rect area);

// Blends a premultiplied top-down BGRA layer (width x height, stride width*4) onto the image
// with its top-left at (x, y). Fully transparent pixels are skipped, so a mostly empty layer is
// cheap; chroma is blended per 2x2 block with the block's average coverage.
void blend_layer(Nv12Image& image, std::span<const unsigned char> bgra, int width, int height, int x, int y);

// The whole image scaled down (or up) into `target` (letterboxed when the aspect differs).
void scale_into(const Nv12Image& source, Nv12Image& target);

// A small top-down premultiplied BGRA copy for the panel's input thumbnails.
std::vector<unsigned char> thumbnail_bgra(const Nv12Picture& picture, int width, int height);
}
