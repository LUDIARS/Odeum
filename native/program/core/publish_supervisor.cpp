#include "publish_supervisor.hpp"

namespace odeum::program {
PublishSupervisor::PublishSupervisor(ReconnectBackoff backoff) : backoff_(backoff) {}

void PublishSupervisor::start() {
    backoff_.reset();
    status_ = PublishStatus::connecting;
    headers_pending_ = true;
}

void PublishSupervisor::live() {
    if (status_ == PublishStatus::stopped) return;
    status_ = PublishStatus::live;
    backoff_.reset();
    headers_pending_ = true;
}

Millis PublishSupervisor::failed(Millis now) {
    if (status_ == PublishStatus::stopped) return 0;
    const auto delay = backoff_.next();
    status_ = PublishStatus::waiting;
    retry_at_ = now + delay;
    headers_pending_ = true;
    return delay;
}

bool PublishSupervisor::due(Millis now) const noexcept { return status_ == PublishStatus::waiting && now >= retry_at_; }

void PublishSupervisor::retrying() {
    if (status_ != PublishStatus::waiting) return;
    status_ = PublishStatus::connecting;
    ++reconnects_;
}

void PublishSupervisor::stop() {
    status_ = PublishStatus::stopped;
    headers_pending_ = true;
}
}
