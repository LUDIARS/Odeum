#pragma once
#include "../core/overlay_state.hpp"
#include <vector>

namespace odeum::presenter {
struct FountainParticle {
    float x{}, y{}, radius{}, alpha{};
    float warmth{}; // 0 = gold, 1 = hot pink; follows how hard the audience is pressing
};

// The rising bubbles for each recent good burst, inside a width x height area (logical px).
// Positions are a pure function of the bursts and the clock, so redrawing the same instant gives
// the same picture. More presses in a burst and a higher heat give more, larger, warmer bubbles.
std::vector<FountainParticle> fountain_particles(const OverlayState& state, Millis now, float width, float height);
}
