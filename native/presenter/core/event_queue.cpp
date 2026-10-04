#include "event_queue.hpp"

namespace odeum::presenter {
void EventQueue::post(std::function<void()> work) {
    std::lock_guard lock(mutex_);
    items_.push_back(std::move(work));
}
std::size_t EventQueue::drain() {
    std::vector<std::function<void()>> ready;
    {
        std::lock_guard lock(mutex_);
        ready.swap(items_);
    }
    for (auto& work : ready) work();
    return ready.size();
}
}
