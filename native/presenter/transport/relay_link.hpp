#pragma once
#include "../core/event_queue.hpp"
#include <odeum/message.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace rtc { class WebSocket; }

namespace odeum::presenter {
// The presenter's WebSocket to the relay. Every callback runs on the UI thread (through the
// event queue), and callbacks of a socket that was replaced or closed are dropped, so the
// owner never sees a late event from an old connection.
class RelayLink {
public:
    struct Callbacks {
        std::function<void()> opened;
        std::function<void(const Message&)> message;
        // A frame the protocol rejects (code from ProtocolError); the link stays open.
        std::function<void(const std::string& code)> invalid;
        std::function<void()> closed;
    };
    RelayLink(EventQueue& ui, Callbacks callbacks);
    ~RelayLink();
    RelayLink(const RelayLink&) = delete;
    RelayLink& operator=(const RelayLink&) = delete;
    // Opens wss://.../v1/ws?ticket=...; replaces any current socket.
    void connect(const std::string& url);
    // Serializes and validates against the protocol before sending; false when not open.
    bool send(const Json& message);
    void close();
    bool open() const;
private:
    EventQueue& ui_;
    Callbacks callbacks_;
    std::shared_ptr<rtc::WebSocket> socket_;
    std::shared_ptr<std::atomic<std::uint64_t>> generation_;
};
}
