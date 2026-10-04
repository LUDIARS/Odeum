#include "media.hpp"
#include <boost/asio/post.hpp>
#include <sstream>

namespace odeum::relay {
namespace {
unsigned byte(const rtc::binary& p, std::size_t i) { return std::to_integer<unsigned>(p.at(i)); }
bool is_rtcp(const rtc::binary& p) { return p.size() >= 4 && (byte(p, 0) >> 6) == 2 && byte(p, 1) >= 192 && byte(p, 1) <= 223; }
void send_packet(const std::shared_ptr<rtc::Track>& track, const rtc::binary& packet) {
    if (!track->isOpen()) return;
    try { track->send(packet); }
    catch (const std::exception&) { /* A concurrent transport close drops this packet; no payload is logged. */ }
}
bool supported_codec(const rtc::Description::Media& media, int pt) {
    const auto* codec = media.rtpMap(pt);
    if (media.type() == "audio") return (codec->format == "opus" || codec->format == "OPUS") && codec->clockRate == 48000;
    if (codec->format != "H264" || codec->clockRate != 90000) return false;
    bool mode = false, profile = false;
    for (const auto& fmt : codec->fmtps) {
        mode |= fmt.find("packetization-mode=1") != std::string::npos;
        profile |= fmt.find("profile-level-id=42e0") != std::string::npos || fmt.find("profile-level-id=42c0") != std::string::npos;
    }
    return mode && profile;
}
void validate_media(const rtc::Description& description, Role role) {
    if (description.hasApplication() || description.mediaCount() < 1 || description.mediaCount() > 2)
        throw ProtocolError("invalid_sdp", "One H264 video and optional Opus audio are required");
    int videos = 0, audios = 0;
    for (int i = 0; i < description.mediaCount(); ++i) {
        auto entry = description.media(i);
        auto media = std::get_if<const rtc::Description::Media*>(&entry);
        if (!media || (*media)->isRemoved()) throw ProtocolError("invalid_sdp", "Invalid media section");
        const auto& m = **media;
        if (m.direction() != (role == Role::presenter ? rtc::Description::Direction::SendOnly : rtc::Description::Direction::RecvOnly))
            throw ProtocolError("invalid_sdp", "Invalid media direction");
        if (m.type() == "video") ++videos; else if (m.type() == "audio") ++audios; else throw ProtocolError("invalid_sdp", "Unsupported media");
        if (role == Role::presenter && m.getSSRCs().size() != 1) throw ProtocolError("invalid_sdp", "Each source must declare one SSRC");
        bool supported = false;
        for (auto pt : m.payloadTypes()) {
            supported |= supported_codec(m, pt);
        }
        if (!supported) throw ProtocolError("invalid_sdp", "Unsupported codec profile");
    }
    if (videos != 1 || audios > 1) throw ProtocolError("invalid_sdp", "Invalid media count");
}
}
bool contains_keyframe_request(const rtc::binary& packet) {
    if (!is_rtcp(packet)) return false;
    for (std::size_t offset = 0; offset + 4 <= packet.size();) {
        auto size = ((byte(packet, offset + 2) << 8) | byte(packet, offset + 3)) * 4 + 4;
        if (size < 4 || offset + size > packet.size()) return false;
        auto fmt = byte(packet, offset) & 31;
        if (byte(packet, offset + 1) == 206 && ((fmt == 1 && size >= 12) || (fmt == 4 && size >= 20))) return true;
        offset += size;
    }
    return false;
}
std::string public_candidate(std::string candidate, const std::string& address) {
    if (address.empty()) return candidate;
    std::istringstream input(candidate); std::vector<std::string> parts; std::string part;
    while (input >> part) parts.push_back(part);
    if (parts.size() < 8 || parts[6] != "typ" || parts[7] != "host" || (parts[2] != "UDP" && parts[2] != "udp")) return candidate;
    parts[4] = address;
    std::ostringstream result; for (std::size_t i = 0; i < parts.size(); ++i) { if (i) result << ' '; result << parts[i]; }
    return result.str();
}
MediaRoom::MediaRoom(boost::asio::io_context& io, const Config& config) : io_(io), config_(config) {}
MediaRoom::~MediaRoom() { for (auto& [id, peer] : peers_) { peer.pc->resetCallbacks(); peer.pc->close(); } }
void MediaRoom::join(std::uint64_t id, Role role, Send send) {
    auto pc = std::make_shared<rtc::PeerConnection>(config_.rtc);
    auto weak = weak_from_this();
    pc->onLocalDescription([weak, id](rtc::Description description) {
        if (auto self = weak.lock()) boost::asio::post(self->io_, [weak, id, description = std::move(description)] {
            if (auto room = weak.lock(); room && room->peers_.contains(id)) {
                auto& peer = room->peers_.at(id);
                peer.send({{"type", "sdp"}, {"sdp", {{"type", description.typeString()}, {"sdp", std::string(description)}}}});
            }
        });
    });
    pc->onLocalCandidate([weak, id](rtc::Candidate candidate) {
        if (auto self = weak.lock()) boost::asio::post(self->io_, [weak, id, candidate = std::move(candidate)] {
            if (auto room = weak.lock(); room && room->peers_.contains(id)) room->peers_.at(id).send({{"type", "candidate"},
                {"candidate", public_candidate(std::string(candidate), room->config_.public_ip)}, {"mid", candidate.mid()}});
        });
    });
    peers_.emplace(id, Peer{role, std::move(send), pc, {}, {}, false});
    if (role == Role::presenter) presenter_ = id; else { subscribe(id); keyframe_pending_ = true; }
}
void MediaRoom::leave(std::uint64_t id) {
    auto it = peers_.find(id); if (it == peers_.end()) return;
    it->second.pc->resetCallbacks(); it->second.pc->close(); peers_.erase(it);
    if (id == presenter_) { presenter_ = 0; sources_.clear(); keyframe_pending_ = false; }
}
void MediaRoom::attach(std::uint64_t id, const std::string& mid, const std::shared_ptr<rtc::Track>& track) {
    auto weak = weak_from_this();
    track->onMessage([weak, id, mid](rtc::message_variant data) {
        auto packet = std::get_if<rtc::binary>(&data); if (!packet) return;
        if (auto room = weak.lock()) {
            if (room->queued_packets_.fetch_add(1) >= 512) { --room->queued_packets_; return; }
            boost::asio::post(room->io_, [weak, id, mid, packet = std::move(*packet)] {
                if (auto self = weak.lock()) { --self->queued_packets_; self->receive(id, mid, packet); }
            });
        }
    });
    track->onOpen([weak] {
        if (auto room = weak.lock()) boost::asio::post(room->io_, [weak] { if (auto self = weak.lock()) self->keyframe_pending_ = true; });
    });
}
void MediaRoom::subscribe(std::uint64_t id) {
    auto& viewer = peers_.at(id); if (sources_.empty() || viewer.offered) return;
    for (const auto& [mid, source] : sources_) {
        auto description = source; description.setDirection(rtc::Description::Direction::SendOnly);
        auto track = viewer.pc->addTrack(description); viewer.tracks.emplace(mid, track); attach(id, mid, track);
    }
    viewer.offered = true; viewer.pc->setLocalDescription(rtc::Description::Type::Offer);
}
void MediaRoom::signal(std::uint64_t id, const Message& message) {
    auto& peer = peers_.at(id);
    if (message.type == MessageType::candidate) {
        auto value = message.body.at("candidate").get<std::string>(); if (value.empty()) return;
        rtc::Candidate candidate(value, message.body.at("mid").get<std::string>());
        if (peer.pc->remoteDescription()) peer.pc->addRemoteCandidate(candidate);
        else { if (peer.candidates.size() >= 128) throw ProtocolError("capacity", "Too many ICE candidates"); peer.candidates.push_back(candidate); }
        return;
    }
    const auto& body = message.body.at("sdp");
    rtc::Description description(body.at("sdp").get<std::string>(), body.at("type").get<std::string>());
    validate_media(description, peer.role);
    if (peer.role == Role::presenter) {
        if (description.type() != rtc::Description::Type::Offer || peer.offered) throw ProtocolError("invalid_sdp", "Presenter must send one offer per connection");
        for (int i = 0; i < description.mediaCount(); ++i) {
            auto source = *std::get<rtc::Description::Media*>(description.media(i));
            // Keep only negotiated primary codecs; RTP remains byte-for-byte unchanged.
            for (auto pt : source.payloadTypes()) {
                if (!supported_codec(source, pt)) source.removeRtpMap(pt);
            }
            sources_.emplace(source.mid(), source);
            source.setDirection(rtc::Description::Direction::RecvOnly);
            auto track = peer.pc->addTrack(source); peer.tracks.emplace(source.mid(), track); attach(id, source.mid(), track);
        }
        peer.offered = true; peer.pc->setRemoteDescription(description); peer.pc->setLocalDescription(rtc::Description::Type::Answer);
        for (auto& [viewer_id, viewer] : peers_) if (viewer.role == Role::viewer) subscribe(viewer_id);
    } else {
        if (description.type() != rtc::Description::Type::Answer || !peer.offered) throw ProtocolError("invalid_sdp", "Viewer must answer the relay offer");
        peer.pc->setRemoteDescription(description);
    }
    for (const auto& candidate : peer.candidates) peer.pc->addRemoteCandidate(candidate);
    peer.candidates.clear(); keyframe_pending_ = true;
}
void MediaRoom::receive(std::uint64_t id, const std::string& mid, rtc::binary packet) {
    if (!peers_.contains(id)) return;
    if (id == presenter_) {
        for (const auto& [viewer_id, peer] : peers_) {
            if (peer.role != Role::viewer) continue;
            if (auto track = peer.tracks.find(mid); track != peer.tracks.end()) send_packet(track->second, packet);
        }
    } else if (contains_keyframe_request(packet)) keyframe_pending_ = true;
    // Receiver reports and NACK are intentionally terminated per viewer; PLI/FIR alone affect the source.
}
void MediaRoom::tick(Millis now) {
    if (!keyframe_pending_ || !presenter_ || now - last_keyframe_ < 1000) return;
    for (const auto& [mid, description] : sources_) {
        if (description.type() != "video") continue;
        auto track = peers_.at(presenter_).tracks.at(mid); if (!track->isOpen()) return;
        const auto ssrc = description.getSSRCs().at(0);
        rtc::binary pli(12, std::byte{0}); pli[0] = std::byte{0x81}; pli[1] = std::byte{206}; pli[3] = std::byte{2};
        for (unsigned i = 0; i < 4; ++i) pli[8 + i] = static_cast<std::byte>((ssrc >> (24 - 8 * i)) & 255);
        send_packet(track, pli);
    }
    last_keyframe_ = now; keyframe_pending_ = false;
}
}
