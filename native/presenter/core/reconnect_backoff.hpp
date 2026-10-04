#pragma once
#include <cstdint>

namespace odeum::presenter {
using Millis = std::int64_t;

// Delay before the next reconnection attempt: initial, then doubled after every failure up to
// the ceiling. A successful connection resets it.
class ReconnectBackoff {
public:
    explicit ReconnectBackoff(Millis initial = 500, Millis ceiling = 30000);
    // Delay for the attempt about to be scheduled; the following call returns the doubled value.
    Millis next();
    void reset() noexcept;
    unsigned attempts() const noexcept { return attempts_; }
private:
    Millis initial_, ceiling_, current_;
    unsigned attempts_ = 0;
};
}
