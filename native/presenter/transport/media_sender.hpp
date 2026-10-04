#pragma once
#include "../core/event_queue.hpp"
#include "../core/video_encoder.hpp"
#include <odeum/message.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace rtc { class PeerConnection; class Track; class Candidate; }

namespace odeum::presenter {
enum class MediaState { idle, negotiating, connected, failed };

// The presenter's PeerConnection to the relay: one sendonly H.264 track (Constrained Baseline,
// packetization-mode=1) and, when enabled, one sendonly Opus track, each with a single SSRC as
// the relay requires. The presenter offers; the relay answers.
class MediaSender {
public:
    struct Callbacks {
        // sdp / candidate messages for the relay (UI thread).
        std::function<void(const Json&)> signal;
        std::function<void(MediaState)> state;
        // Incoming RTCP on the video track, on the network thread, so a keyframe request reaches
        // the encoder without waiting for the UI loop.
        std::function<void(std::span<const std::byte>)> rtcp;
    };
    MediaSender(EventQueue& ui, Callbacks callbacks);
    ~MediaSender();
    MediaSender(const MediaSender&) = delete;
    MediaSender& operator=(const MediaSender&) = delete;
    // ice_servers in the welcome's WebRTC form ({urls, username?, credential?}).
    void start(const Json& ice_servers, bool audio);
    // The relay's answer or one of its candidates.
    void remote(const Message& message);
    // Any thread. Dropped while the track is not open.
    void send_video(const EncodedFrame& frame);
    void send_audio(std::span<const std::byte> packet, std::int64_t timestamp_us);
    void stop();
    MediaState state() const noexcept { return state_; }
private:
    EventQueue& ui_;
    Callbacks callbacks_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::mutex tracks_mutex_;
    std::shared_ptr<rtc::Track> video_, audio_;
    std::int64_t video_origin_us_ = -1, audio_origin_us_ = -1;
    std::vector<std::pair<std::string, std::string>> pending_candidates_;
    std::shared_ptr<std::atomic<std::uint64_t>> generation_;
    MediaState state_ = MediaState::idle;
};
}
