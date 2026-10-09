#include "check.hpp"
#include "core/audio_mixer.hpp"
#include "core/layout.hpp"
#include <cmath>
using namespace odeum::program;

namespace {
template<class Function> void throws(Function action, const char* description) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(description);
}

std::vector<float> constant(float value) { return std::vector<float>(mix_frame_floats, value); }

void layouts() {
    std::array<bool, input_count> connected{true, false, true, true};
    ProgramState state;
    check(state.scene().mode == SceneMode::standby, "Starts on the standby board");
    auto standby = layout(state.scene(), connected, 1920, 1080);
    check(standby.board == Board::standby && standby.tiles.empty(), "Standby shows only the board");

    for (int i = 0; i < input_count; ++i) {
        state.show_full(i);
        const auto full = layout(state.scene(), connected, 1920, 1080);
        check(full.board == Board::none && full.tiles.size() == 1, "Full screen has one tile");
        check(full.tiles[0] == Tile{i, {0, 0, 1920, 1080}, connected[static_cast<std::size_t>(i)]}, "Full screen covers the programme");
    }
    state.show_full(1);
    check(!layout(state.scene(), connected, 1920, 1080).tiles[0].live, "A disconnected input shows the not-connected plate");

    state.show_quad();
    const auto quad = layout(state.scene(), connected, 1920, 1080);
    check(quad.tiles.size() == 4, "Quad has four tiles");
    check(quad.tiles[0] == Tile{0, {0, 0, 960, 540}, true}, "Quad top-left");
    check(quad.tiles[1] == Tile{1, {960, 0, 960, 540}, false}, "Quad top-right, not connected");
    check(quad.tiles[2] == Tile{2, {0, 540, 960, 540}, true}, "Quad bottom-left");
    check(quad.tiles[3] == Tile{3, {960, 540, 960, 540}, true}, "Quad bottom-right");
    // Quarters stay even even when half the size is odd.
    const auto odd = layout(state.scene(), connected, 1300, 730);
    check(odd.tiles[0].area == Rect{0, 0, 650, 364} && odd.tiles[3].area == Rect{650, 364, 650, 366}, "Quarters are even-aligned");
    check(state.scene().focus == 1, "Quad keeps the focused input");

    state.end();
    const auto ended = layout(state.scene(), connected, 1920, 1080);
    check(ended.board == Board::ended && ended.tiles.empty(), "Ended shows only the closing board");

    throws([&] { layout(state.scene(), connected, 1919, 1080); }, "Odd programme size");
    throws([&] { state.show_full(4); }, "Input index range");
    throws([&] { state.volume(2.5); }, "Volume range");
}

void mixer() {
    const std::array<bool, input_count> none{};
    Scene full{SceneMode::full, 2};
    auto audible = audible_inputs(full, none);
    check(!audible[0] && !audible[1] && audible[2] && !audible[3], "Full screen hears the focused input only");
    audible = audible_inputs({SceneMode::quad, 0}, {false, true, false, false});
    check(audible[0] && !audible[1] && audible[2] && audible[3], "Quad hears every unmuted input");
    audible = audible_inputs({SceneMode::standby, 0}, none);
    check(!audible[0] && !audible[1] && !audible[2] && !audible[3], "Standby is silent");
    audible = audible_inputs({SceneMode::ended, 0}, none);
    check(!audible[0] && !audible[1] && !audible[2] && !audible[3], "Ended is silent");

    AudioMixer m;
    m.push(0, constant(0.25f));
    m.push(2, constant(0.5f));
    auto out = m.mix(full, none, 1.0);
    check(out.size() == mix_frame_floats && std::fabs(out[0] - 0.5f) < 1e-6f, "Full screen takes the focused input");
    check(m.mix(full, none, 1.0)[0] == 0.f, "Queues advance even for inputs not heard");

    m.push(0, constant(0.25f));
    m.push(1, constant(0.25f));
    out = m.mix({SceneMode::quad, 0}, {false, true, false, false}, 1.0);
    check(std::fabs(out[5] - 0.25f) < 1e-6f, "Muted input left out of the grid mix");

    // Three loud inputs plus gain would reach 2.4: the frame is scaled into [-1, 1].
    for (int i = 0; i < 3; ++i) m.push(i, constant(i == 1 ? -0.8f : 0.8f));
    out = m.mix({SceneMode::quad, 0}, none, 3.0);
    float peak = 0;
    for (auto s : out) peak = std::max(peak, std::fabs(s));
    check(peak <= 1.0f + 1e-6f && peak > 0.99f, "Clip prevention keeps the peak at full scale");

    m.push(3, constant(0.1f));
    out = m.mix({SceneMode::standby, 0}, none, 1.0);
    check(out[0] == 0.f, "Standby mix is silent");

    AudioMixer bounded(2);
    for (int i = 0; i < 5; ++i) bounded.push(0, constant(static_cast<float>(i) / 10));
    check(std::fabs(bounded.mix({SceneMode::full, 0}, none, 1.0)[0] - 0.3f) < 1e-6f, "Old frames are dropped beyond the queue limit");
    throws([&] { m.push(0, std::vector<float>(10)); }, "Frame size");
}
}

int main() { return run([] { layouts(); mixer(); }); }
