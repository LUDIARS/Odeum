#include "server.hpp"
#include <boost/asio/signal_set.hpp>
#include <csignal>
#include <iostream>

int main() {
    try {
        // Disable library diagnostics: SDP, remote names and ticket material must never reach logs.
        rtc::InitLogger(rtc::LogLevel::None);
        const auto config = odeum::relay::load_config();
        boost::asio::io_context io(1);
        odeum::relay::Server server(io, config);
        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&io](const boost::system::error_code&, int) { io.stop(); });
        server.start(); io.run(); return 0;
    } catch (const std::exception&) {
        // Deliberately do not print external exception text, which can contain credentials.
        std::cerr << "odeum-relay: startup or transport failure; check configuration and dependencies\n";
        return 1;
    }
}
