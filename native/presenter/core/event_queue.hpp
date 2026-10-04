#pragma once
#include <functional>
#include <mutex>
#include <vector>

namespace odeum::presenter {
// Hands work from network, capture and OS callback threads to the UI thread, which owns every
// piece of presenter state. post() never blocks on the UI; drain() runs what was posted so far.
class EventQueue {
public:
    void post(std::function<void()> work);
    // Returns how many items ran. Work posted while draining runs on the next drain.
    std::size_t drain();
private:
    std::mutex mutex_;
    std::vector<std::function<void()>> items_;
};
}
