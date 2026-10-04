#pragma once
#include "reconnect_backoff.hpp"
#include <string>

namespace odeum::presenter {
enum class LinkStatus { idle, connecting, connected, waiting, ticket_expired };

struct RetryDecision {
    bool retry{};
    bool ticket_expired{};
    Millis delay_ms{};
};

// Decides what happens after the relay link closes. The relay accepts a ticket only before its
// exp (at most five minutes after GLab issued it), so once the next attempt would land after exp
// the monitor stops and the presenter is told to reissue the link in GLab instead of retrying.
class ConnectionMonitor {
public:
    explicit ConnectionMonitor(ReconnectBackoff backoff = ReconnectBackoff{});
    // A new ticket (launch link) replaces the old one and starts over.
    void start(std::int64_t ticket_exp_epoch_s);
    void welcomed();
    // The relay sent an error before closing; kept so the panel can say why.
    void rejected(std::string code);
    RetryDecision closed(std::int64_t now_epoch_s, Millis now_ms);
    // True once a scheduled retry is due; the caller then reconnects and calls retrying().
    bool due(Millis now_ms) const noexcept;
    void retrying() noexcept;
    void stop() noexcept;
    LinkStatus status() const noexcept { return status_; }
    Millis retry_at() const noexcept { return retry_at_; }
    // Attempts since the last welcome that never reached one. A replayed ticket fails like this,
    // so several in a row suggest a new link is needed even before exp.
    unsigned failures() const noexcept { return failures_; }
    bool ticket_suspect() const noexcept { return failures_ >= 3; }
    const std::string& last_error() const noexcept { return last_error_; }
private:
    ReconnectBackoff backoff_;
    LinkStatus status_ = LinkStatus::idle;
    std::int64_t ticket_exp_ = 0;
    Millis retry_at_ = 0;
    unsigned failures_ = 0;
    bool welcomed_ = false;
    std::string last_error_;
};
}
