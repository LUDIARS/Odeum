#include "layout.hpp"
#include <stdexcept>

namespace odeum::program {
ProgramLayout layout(const Scene& scene, const std::array<bool, input_count>& connected, int width, int height) {
    if (width <= 0 || height <= 0 || width % 2 || height % 2) throw std::invalid_argument("Programme size must be positive and even");
    ProgramLayout result;
    switch (scene.mode) {
    case SceneMode::standby: result.board = Board::standby; break;
    case SceneMode::ended: result.board = Board::ended; break;
    case SceneMode::full:
        result.tiles.push_back({scene.focus, {0, 0, width, height}, connected[input_at(scene.focus)]});
        break;
    case SceneMode::quad: {
        // NV12 needs even edges; the right/bottom quarters take whatever the halves leave.
        const int half_w = (width / 2) & ~1, half_h = (height / 2) & ~1;
        for (int i = 0; i < input_count; ++i) {
            const int column = i % 2, row = i / 2;
            const Rect area{column * half_w, row * half_h, column ? width - half_w : half_w, row ? height - half_h : half_h};
            result.tiles.push_back({i, area, connected[static_cast<std::size_t>(i)]});
        }
        break;
    }
    }
    return result;
}
}
