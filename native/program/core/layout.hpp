#pragma once
#include "program_state.hpp"
#include <vector>

namespace odeum::program {
struct Rect {
    int x{}, y{}, width{}, height{};
    bool operator==(const Rect&) const = default;
};

// A board that covers the whole programme instead of inputs.
enum class Board { none, standby, ended };

// One input's place on the programme. A tile whose input is not connected shows the
// "input not connected" plate, never a held last frame.
struct Tile {
    int input{};
    Rect area;
    bool live{};
    bool operator==(const Tile&) const = default;
};

struct ProgramLayout {
    std::vector<Tile> tiles;
    Board board = Board::none;
    bool operator==(const ProgramLayout&) const = default;
};

// full: the focused input over the whole programme. quad: inputs 1..4 left-to-right,
// top-to-bottom in even-sized quarters. standby / ended: no tiles, the board instead.
// Throws std::invalid_argument for a programme size that is not positive and even.
ProgramLayout layout(const Scene& scene, const std::array<bool, input_count>& connected, int width, int height);
}
