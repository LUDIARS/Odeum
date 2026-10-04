#include "reconnect_backoff.hpp"
#include <algorithm>
#include <stdexcept>

namespace odeum::presenter {
ReconnectBackoff::ReconnectBackoff(Millis initial, Millis ceiling) : initial_(initial), ceiling_(ceiling), current_(initial) {
    if (initial <= 0 || ceiling < initial) throw std::invalid_argument("Backoff needs 0 < initial <= ceiling");
}
Millis ReconnectBackoff::next() {
    const auto delay = current_;
    current_ = std::min(ceiling_, current_ * 2);
    ++attempts_;
    return delay;
}
void ReconnectBackoff::reset() noexcept {
    current_ = initial_;
    attempts_ = 0;
}
}
