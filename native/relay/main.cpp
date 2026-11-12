#include "server.hpp"
#include "turn_refresher.hpp"
#include <boost/asio/signal_set.hpp>
#include <chrono>
#include <csignal>
#include <future>
#include <iostream>

int main() {
    try {
        // Disable library diagnostics: SDP, remote names and ticket material must never reach logs.
        rtc::InitLogger(rtc::LogLevel::None);
        const auto config = odeum::relay::load_config();
        {
            boost::asio::io_context io(1);
            odeum::relay::Server server(io, config);
            boost::asio::signal_set signals(io, SIGINT, SIGTERM);
            signals.async_wait([&io](const boost::system::error_code&, int) { io.stop(); });
            // Short-lived Cloudflare TURN credentials: welcomes and peer connections pick up each refresh.
            std::shared_ptr<odeum::relay::TurnRefresher> turn;
            if (config.cloudflare) {
                turn = std::make_shared<odeum::relay::TurnRefresher>(io, *config.cloudflare, config.ice);
                turn->start();
            }
            server.start(); io.run();
        }
        // Join libdatachannel's workers before exit(): OpenSSL's atexit cleanup must not run while a
        // worker still uses OpenSSL (it crashed the relay's tests at exit, see tests/rtc_exit.hpp).
        if (rtc::Cleanup().wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
            std::cerr << "odeum-relay: WebRTC cleanup did not finish\n";
            return 1;
        }
        return 0;
    } catch (const std::exception&) {
        // Deliberately do not print external exception text, which can contain credentials.
        std::cerr << "odeum-relay: startup or transport failure; check configuration and dependencies\n";
        return 1;
    }
}
