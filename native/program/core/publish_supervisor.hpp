#pragma once
#include "core/reconnect_backoff.hpp"

namespace odeum::program {
using presenter::Millis;
using presenter::ReconnectBackoff;

enum class PublishStatus { stopped, connecting, live, waiting };

// Decides when the YouTube publish reconnects: 1 s after the first failure, doubling to 30 s,
// back to 1 s once a publish starts. After every (re)connection the stream restarts from the
// metadata and sequence headers and waits for a keyframe, which headers_pending() reports.
class PublishSupervisor {
public:
    explicit PublishSupervisor(ReconnectBackoff backoff = ReconnectBackoff(1000, 30000));
    // The operator pressed start: connect now.
    void start();
    // The server answered NetStream.Publish.Start.
    void live();
    // The connection or the publish failed. Returns the delay before the next attempt.
    Millis failed(Millis now);
    // True when waiting and the retry time has come.
    bool due(Millis now) const noexcept;
    // The next attempt begins.
    void retrying();
    void stop();
    // Until headers_sent(), the stream must open with onMetaData, AVC/AAC sequence headers and
    // a keyframe.
    bool headers_pending() const noexcept { return headers_pending_; }
    void headers_sent() noexcept { headers_pending_ = false; }
    PublishStatus status() const noexcept { return status_; }
    Millis retry_at() const noexcept { return retry_at_; }
    unsigned reconnects() const noexcept { return reconnects_; }
private:
    ReconnectBackoff backoff_;
    PublishStatus status_ = PublishStatus::stopped;
    Millis retry_at_ = 0;
    unsigned reconnects_ = 0;
    bool headers_pending_ = true;
};
}
