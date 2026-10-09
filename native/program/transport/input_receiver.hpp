#pragma once
#include "core/event_queue.hpp"
#include "../core/program_state.hpp"
#include <odeum/message.hpp>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace rtc { class PeerConnection; class Track; }

namespace odeum::program {
// The receiving PeerConnection of the producer: the relay offers one H.264 and (optionally) one
// Opus section per connected input (mids `<slot>-<n>`), the program answers recvonly. Frames
// arrive depacketized on libdatachannel's threads; signalling and state changes on the UI thread.
class InputReceiver {
public:
    struct Callbacks {
        // answer / candidate messages for the relay (UI thread).
        std::function<void(const Json&)> signal;
        // Network threads: one H.264 access unit (Annex-B) or one Opus packet of an input.
        std::function<void(int input, std::span<const std::byte> access_unit)> video;
        std::function<void(int input, std::span<const std::byte> packet)> audio;
        // UI thread: an input's tracks opened (live) or closed.
        std::function<void(int input, bool live)> track;
    };
    InputReceiver(presenter::EventQueue& ui, Callbacks callbacks);
    ~InputReceiver();
    InputReceiver(const InputReceiver&) = delete;
    InputReceiver& operator=(const InputReceiver&) = delete;
    // ICE servers from the welcome; replaces any previous connection.
    void start(const Json& ice_servers);
    // The relay's offer (initial or renegotiated) or one of its candidates.
    void remote(const Message& message);
    // {"type":"track.closed","mids":[...]}: those sections stop before the relay re-offers.
    void closed(const Json& mids);
    // Asks the input's sender for an IDR (PLI through the relay).
    void request_keyframe(int input);
    void stop();
private:
    void attach(const std::shared_ptr<rtc::Track>& track);
    presenter::EventQueue& ui_;
    Callbacks callbacks_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::mutex tracks_mutex_;
    std::map<std::string, std::shared_ptr<rtc::Track>> tracks_; // by mid
    std::shared_ptr<std::atomic<std::uint64_t>> generation_;
};
}
