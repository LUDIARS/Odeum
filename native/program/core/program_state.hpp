#pragma once
#include <array>
#include <cstddef>

namespace odeum::program {
// The relay's input slots this program switches between (input1..input4).
inline constexpr int input_count = 4;

enum class SceneMode { full, quad, standby, ended };

// What the program shows: one input full screen, all four in a grid, the standby board before
// the show, or the closing board after it.
struct Scene {
    SceneMode mode = SceneMode::standby;
    int focus = 0; // 0-based input shown full screen; kept while other modes are on air
    bool operator==(const Scene&) const = default;
};

// The operator's choices and what the relay says about the inputs. Owned by the UI thread; the
// render and audio threads take copies.
class ProgramState {
public:
    // From the relay's presence (slots.inputN) and the receiving tracks.
    void input_connected(int index, bool connected);
    bool connected(int index) const;
    const std::array<bool, input_count>& connections() const noexcept { return connected_; }

    void show_full(int index);
    void show_quad() noexcept { scene_.mode = SceneMode::quad; }
    void standby() noexcept { scene_.mode = SceneMode::standby; }
    void end() noexcept { scene_.mode = SceneMode::ended; }
    const Scene& scene() const noexcept { return scene_; }

    void mute(int index, bool muted);
    bool muted(int index) const;
    const std::array<bool, input_count>& mutes() const noexcept { return muted_; }
    // Programme gain, 0..2 (1 = unchanged). Throws std::invalid_argument outside the range.
    void volume(double gain);
    double volume() const noexcept { return volume_; }
private:
    Scene scene_;
    std::array<bool, input_count> connected_{}, muted_{};
    double volume_ = 1.0;
};

// 0..3 for a valid input index; throws std::out_of_range otherwise.
std::size_t input_at(int index);
}
