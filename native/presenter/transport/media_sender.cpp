#include "media_sender.hpp"
#include <rtc/rtc.hpp>
#include <chrono>
#include <random>

namespace odeum::presenter {
namespace {
constexpr int video_payload_type = 102, audio_payload_type = 111;
// Constrained Baseline (42e0) at level 4.0 so 1080p30 is within the declared level; the relay
// accepts any 42e0/42c0 profile with packetization-mode=1.
constexpr const char* h264_profile = "profile-level-id=42e028;packetization-mode=1;level-asymmetry-allowed=1";
constexpr const char* cname = "odeum-presenter";

std::vector<rtc::IceServer> ice_servers(const Json& list) {
    std::vector<rtc::IceServer> result;
    if (!list.is_array()) return result;
    for (const auto& entry : list) {
        if (!entry.is_object() || !entry.contains("urls")) continue;
        std::vector<std::string> urls;
        if (entry["urls"].is_string()) urls.push_back(entry["urls"].get<std::string>());
        else if (entry["urls"].is_array())
            for (const auto& url : entry["urls"]) if (url.is_string()) urls.push_back(url.get<std::string>());
        for (const auto& url : urls) {
            try {
                rtc::IceServer server(url);
                if (server.type == rtc::IceServer::Type::Turn) {
                    if (entry.contains("username") && entry["username"].is_string()) server.username = entry["username"].get<std::string>();
                    if (entry.contains("credential") && entry["credential"].is_string()) server.password = entry["credential"].get<std::string>();
                }
                result.push_back(std::move(server));
            } catch (const std::exception&) {
                // An unreadable entry only loses that server; the relay's host candidates remain.
            }
        }
    }
    return result;
}

std::uint32_t random_ssrc() {
    static std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<std::uint32_t> distribution(1, 0xfffffffe);
    return distribution(generator);
}

MediaState media_state(rtc::PeerConnection::State state) {
    switch (state) {
    case rtc::PeerConnection::State::Connected: return MediaState::connected;
    case rtc::PeerConnection::State::Failed:
    case rtc::PeerConnection::State::Disconnected:
    case rtc::PeerConnection::State::Closed: return MediaState::failed;
    default: return MediaState::negotiating;
    }
}

void send_frame(const std::shared_ptr<rtc::Track>& track, std::span<const std::byte> data, double seconds) {
    if (!track || !track->isOpen()) return;
    try { track->sendFrame(data.data(), data.size(), rtc::FrameInfo(std::chrono::duration<double>(seconds))); }
    catch (const std::exception&) { /* A transport closing under us drops this frame. */ }
}
}

MediaSender::MediaSender(EventQueue& ui, Callbacks callbacks)
    : ui_(ui), callbacks_(std::move(callbacks)), generation_(std::make_shared<std::atomic<std::uint64_t>>(0)) {}

MediaSender::~MediaSender() { stop(); }

void MediaSender::start(const Json& ice, bool audio) {
    stop();
    const auto generation = ++*generation_;
    rtc::Configuration config;
    config.iceServers = ice_servers(ice);
    config.disableAutoNegotiation = true;
    pc_ = std::make_shared<rtc::PeerConnection>(config);
    state_ = MediaState::negotiating;
    auto current = generation_;
    auto& ui = ui_;
    auto post = [&ui, current, generation](std::function<void()> work) {
        ui.post([current, generation, work = std::move(work)] { if (current->load() == generation) work(); });
    };
    pc_->onLocalDescription([this, post](rtc::Description description) {
        Json message = {{"type", "sdp"}, {"sdp", {{"type", description.typeString()}, {"sdp", std::string(description)}}}};
        post([this, message] { if (callbacks_.signal) callbacks_.signal(message); });
    });
    pc_->onLocalCandidate([this, post](rtc::Candidate candidate) {
        Json message = {{"type", "candidate"}, {"candidate", std::string(candidate)}, {"mid", candidate.mid()}};
        post([this, message] { if (callbacks_.signal) callbacks_.signal(message); });
    });
    pc_->onStateChange([this, post](rtc::PeerConnection::State state) {
        post([this, next = media_state(state)] {
            if (state_ == next) return;
            state_ = next;
            if (callbacks_.state) callbacks_.state(next);
        });
    });

    const auto video_ssrc = random_ssrc();
    rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
    video.addH264Codec(video_payload_type, h264_profile);
    video.addSSRC(video_ssrc, cname, "odeum", "odeum-video");
    auto video_track = pc_->addTrack(video);
    auto video_config = std::make_shared<rtc::RtpPacketizationConfig>(video_ssrc, cname, video_payload_type, rtc::H264RtpPacketizer::ClockRate);
    video_track->setMediaHandler(std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, video_config));
    video_track->chainMediaHandler(std::make_shared<rtc::RtcpSrReporter>(video_config));
    // Copied, not referenced through `this`: it runs on the network thread and must stay valid
    // even while the UI thread is tearing the sender down.
    video_track->onMessage([rtcp = callbacks_.rtcp](rtc::message_variant data) {
        if (auto packet = std::get_if<rtc::binary>(&data); packet && rtcp) rtcp(*packet);
    });

    std::shared_ptr<rtc::Track> audio_track;
    if (audio) {
        const auto audio_ssrc = random_ssrc();
        rtc::Description::Audio sound("audio", rtc::Description::Direction::SendOnly);
        sound.addOpusCodec(audio_payload_type);
        sound.addSSRC(audio_ssrc, cname, "odeum", "odeum-audio");
        audio_track = pc_->addTrack(sound);
        auto audio_config = std::make_shared<rtc::RtpPacketizationConfig>(audio_ssrc, cname, audio_payload_type, rtc::OpusRtpPacketizer::DefaultClockRate);
        audio_track->setMediaHandler(std::make_shared<rtc::OpusRtpPacketizer>(audio_config));
        audio_track->chainMediaHandler(std::make_shared<rtc::RtcpSrReporter>(audio_config));
    }
    {
        std::lock_guard lock(tracks_mutex_);
        video_ = video_track;
        audio_ = audio_track;
        video_origin_us_ = audio_origin_us_ = -1;
    }
    pending_candidates_.clear();
    pc_->setLocalDescription(rtc::Description::Type::Offer);
}

void MediaSender::remote(const Message& message) {
    if (!pc_) return;
    if (message.type == MessageType::sdp) {
        const auto& sdp = message.body.at("sdp");
        if (sdp.at("type").get<std::string>() != "answer") throw ProtocolError("invalid_sdp", "The relay must answer the presenter's offer");
        pc_->setRemoteDescription(rtc::Description(sdp.at("sdp").get<std::string>(), "answer"));
        for (const auto& [candidate, mid] : pending_candidates_) pc_->addRemoteCandidate(rtc::Candidate(candidate, mid));
        pending_candidates_.clear();
    } else if (message.type == MessageType::candidate) {
        auto candidate = message.body.at("candidate").get<std::string>();
        auto mid = message.body.at("mid").get<std::string>();
        if (candidate.empty()) return; // end of candidates
        if (pc_->remoteDescription()) pc_->addRemoteCandidate(rtc::Candidate(candidate, mid));
        else if (pending_candidates_.size() < 128) pending_candidates_.emplace_back(std::move(candidate), std::move(mid));
    }
}

void MediaSender::send_video(const EncodedFrame& frame) {
    std::shared_ptr<rtc::Track> track;
    double seconds = 0;
    {
        std::lock_guard lock(tracks_mutex_);
        track = video_;
        if (video_origin_us_ < 0) video_origin_us_ = frame.timestamp_us;
        seconds = static_cast<double>(frame.timestamp_us - video_origin_us_) / 1e6;
    }
    send_frame(track, frame.data, seconds);
}

void MediaSender::send_audio(std::span<const std::byte> packet, std::int64_t timestamp_us) {
    std::shared_ptr<rtc::Track> track;
    double seconds = 0;
    {
        std::lock_guard lock(tracks_mutex_);
        track = audio_;
        if (audio_origin_us_ < 0) audio_origin_us_ = timestamp_us;
        seconds = static_cast<double>(timestamp_us - audio_origin_us_) / 1e6;
    }
    send_frame(track, packet, seconds);
}

void MediaSender::stop() {
    ++*generation_;
    {
        std::lock_guard lock(tracks_mutex_);
        video_.reset();
        audio_.reset();
    }
    pending_candidates_.clear();
    state_ = MediaState::idle;
    if (!pc_) return;
    pc_->resetCallbacks();
    pc_->close();
    pc_.reset();
}
}
