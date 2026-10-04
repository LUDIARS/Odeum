#include "relay_link.hpp"
#include <rtc/websocket.hpp>
#include <chrono>

namespace odeum::presenter {
RelayLink::RelayLink(EventQueue& ui, Callbacks callbacks)
    : ui_(ui), callbacks_(std::move(callbacks)), generation_(std::make_shared<std::atomic<std::uint64_t>>(0)) {}

RelayLink::~RelayLink() { close(); }

void RelayLink::connect(const std::string& url) {
    close();
    const auto generation = ++*generation_;
    rtc::WebSocketConfiguration config;
    config.connectionTimeout = std::chrono::seconds(10);
    // The relay pings every few seconds and drops a socket idle for 60 s; answer from our side too.
    config.pingInterval = std::chrono::seconds(15);
    config.maxMessageSize = max_message_bytes;
    socket_ = std::make_shared<rtc::WebSocket>(config);
    auto current = generation_;
    auto& ui = ui_;
    auto* callbacks = &callbacks_;
    // Guards every hop onto the UI thread: the link may have moved on (or been destroyed with its
    // generation counter still alive in this closure) by the time the work runs.
    auto post = [&ui, current, generation](std::function<void()> work) {
        ui.post([current, generation, work = std::move(work)] { if (current->load() == generation) work(); });
    };
    socket_->onOpen([post, callbacks] { post([callbacks] { if (callbacks->opened) callbacks->opened(); }); });
    socket_->onClosed([post, callbacks] { post([callbacks] { if (callbacks->closed) callbacks->closed(); }); });
    // An error is always followed by onClosed, which drives reconnection; nothing is logged
    // because the URL carries the ticket.
    socket_->onError([](std::string) {});
    socket_->onMessage([post, callbacks](rtc::message_variant data) {
        auto text = std::get_if<std::string>(&data);
        if (!text) { post([callbacks] { if (callbacks->invalid) callbacks->invalid("invalid_message"); }); return; }
        try {
            auto message = parse_message(*text);
            post([callbacks, message = std::move(message)] { if (callbacks->message) callbacks->message(message); });
        } catch (const ProtocolError& error) {
            post([callbacks, code = error.code] { if (callbacks->invalid) callbacks->invalid(code); });
        }
    });
    socket_->open(url);
}

bool RelayLink::send(const Json& message) {
    if (!socket_ || !socket_->isOpen()) return false;
    auto wire = message.dump();
    parse_message(wire); // throws ProtocolError for anything the relay would refuse
    return socket_->send(std::move(wire));
}

void RelayLink::close() {
    ++*generation_;
    if (!socket_) return;
    socket_->resetCallbacks();
    socket_->close();
    socket_.reset();
}

bool RelayLink::open() const { return socket_ && socket_->isOpen(); }
}
