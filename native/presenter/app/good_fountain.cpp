#include "good_fountain.hpp"
#include <algorithm>
#include <cmath>

namespace odeum::presenter {
namespace {
// Small deterministic hash -> [0,1).
float unit(std::uint64_t seed) {
    seed ^= seed >> 33; seed *= 0xff51afd7ed558ccdULL;
    seed ^= seed >> 33; seed *= 0xc4ceb9fe1a85ec53ULL;
    seed ^= seed >> 33;
    return static_cast<float>(seed & 0xffffff) / static_cast<float>(0x1000000);
}
constexpr Millis stagger_ms = 45;
}

std::vector<FountainParticle> fountain_particles(const OverlayState& state, Millis now, float width, float height) {
    std::vector<FountainParticle> particles;
    const auto heat = static_cast<float>(state.heat(now));
    const auto lifetime = state.limits().burst_lifetime;
    const auto cap = static_cast<std::int64_t>(4 + std::lround(heat * 20));
    for (const auto& burst : state.bursts()) {
        const auto count = std::clamp<std::int64_t>(burst.count, 1, cap);
        for (std::int64_t i = 0; i < count; ++i) {
            const auto start = burst.at + i * stagger_ms;
            const auto span = lifetime - (cap - 1) * stagger_ms;
            if (now < start || span <= 0) continue;
            const float t = static_cast<float>(now - start) / static_cast<float>(span);
            if (t >= 1) continue;
            const auto seed = static_cast<std::uint64_t>(burst.at) * 131 + static_cast<std::uint64_t>(i);
            const float lane = 0.12f + 0.76f * unit(seed);
            const float sway = std::sin((t * 3 + unit(seed + 7)) * 3.14159f) * 10 * (0.5f + heat);
            const float rise = height * (0.55f + 0.45f * unit(seed + 13));
            FountainParticle p;
            p.x = width * lane + sway;
            p.y = height - 8 - rise * (1 - (1 - t) * (1 - t)); // eases out like a bubble slowing down
            p.radius = (5 + 9 * heat) * (0.7f + 0.6f * unit(seed + 29)) * (1 - 0.35f * t);
            p.alpha = 1 - t * t;
            p.warmth = std::clamp(heat * (0.6f + 0.6f * unit(seed + 41)), 0.0f, 1.0f);
            particles.push_back(p);
        }
    }
    return particles;
}
}
