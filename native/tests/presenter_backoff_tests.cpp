#include "check.hpp"
#include "core/connection_monitor.hpp"
using namespace odeum::presenter;

int main() { return run([] {
    ReconnectBackoff backoff;
    check(backoff.next() == 500 && backoff.next() == 1000 && backoff.next() == 2000, "Doubling");
    for (int i = 0; i < 10; ++i) backoff.next();
    check(backoff.next() == 30000, "Ceiling");
    backoff.reset();
    check(backoff.next() == 500 && backoff.attempts() == 1, "Reset");
    bool refused = false;
    try { ReconnectBackoff(0, 10); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, "Invalid backoff");

    // Ticket valid until epoch 1000 s; the clock is injected.
    ConnectionMonitor monitor;
    monitor.start(1000);
    check(monitor.status() == LinkStatus::connecting, "Connecting");
    monitor.welcomed();
    auto decision = monitor.closed(900, 50000);
    check(decision.retry && !decision.ticket_expired && decision.delay_ms == 500, "First retry");
    check(monitor.status() == LinkStatus::waiting && !monitor.due(50499) && monitor.due(50500), "Retry time");
    monitor.retrying();
    decision = monitor.closed(901, 51000);
    check(decision.retry && decision.delay_ms == 1000 && monitor.failures() == 1, "Backoff grows without welcome");
    monitor.retrying();
    monitor.closed(902, 52000);
    monitor.retrying();
    monitor.closed(904, 54000);
    check(monitor.ticket_suspect(), "Repeated failures suggest a new link");
    monitor.retrying();
    monitor.welcomed();
    check(monitor.failures() == 0, "Welcome clears failures");
    decision = monitor.closed(905, 55000);
    check(decision.delay_ms == 500, "Welcome resets backoff");

    // A retry 500 ms before exp still fits; one that would land at or after exp stops and asks
    // for a new link.
    ConnectionMonitor late(ReconnectBackoff(500, 30000));
    late.start(1000);
    late.welcomed();
    decision = late.closed(998, 0);
    check(decision.retry, "Retry before exp");
    late.retrying();
    decision = late.closed(999, 0); // 999 s + 1000 ms reaches exp
    check(!decision.retry && decision.ticket_expired && late.status() == LinkStatus::ticket_expired, "Expired ticket");
    decision = late.closed(999, 0);
    check(!decision.retry && decision.ticket_expired, "Stays expired");
    late.start(2000);
    check(late.status() == LinkStatus::connecting, "New link restarts");

    ConnectionMonitor stopped;
    stopped.start(1000);
    stopped.stop();
    decision = stopped.closed(10, 0);
    check(!decision.retry && !decision.ticket_expired, "Stopped link does not retry");
}); }
