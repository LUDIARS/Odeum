#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace odeum::presenter {
struct Letterbox {
    int x{}, y{}, width{}, height{}; // where the picture lands inside the output, even-aligned
    bool operator==(const Letterbox&) const = default;
};

// The largest rectangle with the source's aspect ratio that fits the output, centred.
Letterbox fit_letterbox(int source_width, int source_height, int output_width, int output_height);

// Top-down BGRA8 (any stride) to NV12 (BT.709 limited range) at the output size, keeping the
// aspect ratio with black bars. The output is a Y plane of out_w*out_h followed by an
// interleaved UV plane of out_w*out_h/2. Box-averages when shrinking so text stays legible.
// Throws std::invalid_argument for odd output sizes or buffers that are too small.
void bgra_to_nv12(std::span<const std::uint8_t> bgra, int width, int height, int stride,
                  std::span<std::uint8_t> nv12, int out_width, int out_height);
}
