#include "input_receiver.hpp"
#include "../core/producer_routing.hpp"
#include "transport/ice_servers.hpp"
#include <rtc/rtc.hpp>

namespace odeum::program {
InputReceiver::InputReceiver(presenter::EventQueue& ui, Callbacks callbacks)
    : ui_(ui), callbacks_(std::move(callbacks)), generation_(std::make_shared<std::atomic<std::uint64_t>>(0)) {}

InputReceiver::~InputReceiver() { stop(); }

void InputReceiver::start(const Json& ice) {
    stop();
    const auto generation = ++*generation_;
    rtc::Configuration config;
    config.iceServers = presenter::ice_servers(ice);
    config.disableAutoNegotiation = true;
    pc_ = std::make_shared<rtc::PeerConnection>(config);
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
    pc_->onTrack([this, post](std::shared_ptr<rtc::Track> track) {
        attach(track);
        const auto input = input_of_mid(track->mid());
        if (!input) return;
        // Opened on the network thread; the UI learns about it and the sender is asked for an IDR.
        track->onOpen([this, post, input = *input, weak = std::weak_ptr<rtc::Track>(track)] {
            if (auto t = weak.lock(); t && t->description().type() == "video") t->requestKeyframe();
            post([this, input] { if (callbacks_.track) callbacks_.track(input, true); });
        });
    });
}

void InputReceiver::attach(const std::shared_ptr<rtc::Track>& track) {
    const auto input = input_of_mid(track->mid());
    if (!input) return;
    const bool video = track->description().type() == "video";
    if (video) track->setMediaHandler(std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence));
    else track->setMediaHandler(std::make_shared<rtc::OpusRtpDepacketizer>());
    // Receiver reports and the PLI that requestKeyframe() sends.
    track->chainMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
    // Copied, not referenced through `this`: frames arrive on network threads.
    auto sink_video = callbacks_.video;
    auto sink_audio = callbacks_.audio;
    track->onFrame([video, index = *input, sink_video, sink_audio](rtc::binary data, rtc::FrameInfo) {
        const std::span<const std::byte> bytes(data.data(), data.size());
        if (video) { if (sink_video) sink_video(index, bytes); }
        else if (sink_audio) sink_audio(index, bytes);
    });
    std::lock_guard lock(tracks_mutex_);
    tracks_[track->mid()] = track;
}

void InputReceiver::remote(const Message& message) {
    if (!pc_) return;
    if (message.type == MessageType::sdp) {
        const auto& sdp = message.body.at("sdp");
        if (sdp.at("type").get<std::string>() != "offer") throw ProtocolError("invalid_sdp", "The relay must offer the inputs");
        pc_->setRemoteDescription(rtc::Description(sdp.at("sdp").get<std::string>(), "offer"));
        pc_->setLocalDescription(rtc::Description::Type::Answer);
    } else if (message.type == MessageType::candidate) {
        const auto candidate = message.body.at("candidate").get<std::string>();
        if (candidate.empty() || !pc_->remoteDescription()) return;
        pc_->addRemoteCandidate(rtc::Candidate(candidate, message.body.at("mid").get<std::string>()));
    }
}

void InputReceiver::closed(const Json& mids) {
    std::vector<int> inputs;
    {
        std::lock_guard lock(tracks_mutex_);
        for (const auto& mid : mids) {
            if (!mid.is_string()) continue;
            const auto name = mid.get<std::string>();
            const auto found = tracks_.find(name);
            if (found == tracks_.end()) continue;
            found->second->onFrame(nullptr);
            tracks_.erase(found);
            if (auto input = input_of_mid(name)) inputs.push_back(*input);
        }
    }
    for (int input : inputs) if (callbacks_.track) callbacks_.track(input, false);
}

void InputReceiver::request_keyframe(int input) {
    std::lock_guard lock(tracks_mutex_);
    for (const auto& [mid, track] : tracks_)
        if (input_of_mid(mid) == input && track->description().type() == "video" && track->isOpen()) track->requestKeyframe();
}

void InputReceiver::stop() {
    ++*generation_;
    {
        std::lock_guard lock(tracks_mutex_);
        for (auto& [mid, track] : tracks_) track->onFrame(nullptr);
        tracks_.clear();
    }
    if (!pc_) return;
    pc_->resetCallbacks();
    pc_->close();
    pc_.reset();
}
}
