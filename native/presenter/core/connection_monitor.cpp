#include "connection_monitor.hpp"

namespace odeum::presenter {
ConnectionMonitor::ConnectionMonitor(ReconnectBackoff backoff) : backoff_(backoff) {}
void ConnectionMonitor::start(std::int64_t ticket_exp_epoch_s) {
    ticket_exp_ = ticket_exp_epoch_s;
    backoff_.reset();
    status_ = LinkStatus::connecting;
    failures_ = 0;
    welcomed_ = false;
    last_error_.clear();
}
void ConnectionMonitor::welcomed() {
    status_ = LinkStatus::connected;
    backoff_.reset();
    failures_ = 0;
    welcomed_ = true;
    last_error_.clear();
}
void ConnectionMonitor::rejected(std::string code) { last_error_ = std::move(code); }
RetryDecision ConnectionMonitor::closed(std::int64_t now_epoch_s, Millis now_ms) {
    if (status_ == LinkStatus::idle || status_ == LinkStatus::ticket_expired) return {false, status_ == LinkStatus::ticket_expired, 0};
    if (!welcomed_) ++failures_;
    welcomed_ = false;
    const auto delay = backoff_.next();
    // The relay checks exp when the connection is established, so the retry must land before it.
    if (now_epoch_s * 1000 + delay >= ticket_exp_ * 1000) {
        status_ = LinkStatus::ticket_expired;
        return {false, true, 0};
    }
    status_ = LinkStatus::waiting;
    retry_at_ = now_ms + delay;
    return {true, false, delay};
}
bool ConnectionMonitor::due(Millis now_ms) const noexcept { return status_ == LinkStatus::waiting && now_ms >= retry_at_; }
void ConnectionMonitor::retrying() noexcept { if (status_ == LinkStatus::waiting) status_ = LinkStatus::connecting; }
void ConnectionMonitor::stop() noexcept {
    status_ = LinkStatus::idle;
    welcomed_ = false;
}
}
