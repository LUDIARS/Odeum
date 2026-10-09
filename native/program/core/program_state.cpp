#include "program_state.hpp"
#include <stdexcept>

namespace odeum::program {
std::size_t input_at(int index) {
    if (index < 0 || index >= input_count) throw std::out_of_range("Input index must be 0..3");
    return static_cast<std::size_t>(index);
}

void ProgramState::input_connected(int index, bool connected) { connected_[input_at(index)] = connected; }
bool ProgramState::connected(int index) const { return connected_[input_at(index)]; }

void ProgramState::show_full(int index) {
    input_at(index);
    scene_.mode = SceneMode::full;
    scene_.focus = index;
}

void ProgramState::mute(int index, bool muted) { muted_[input_at(index)] = muted; }
bool ProgramState::muted(int index) const { return muted_[input_at(index)]; }

void ProgramState::volume(double gain) {
    if (!(gain >= 0.0 && gain <= 2.0)) throw std::invalid_argument("Programme volume must be 0..2");
    volume_ = gain;
}
}
