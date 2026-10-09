#include "media.hpp"
#include "slots.hpp"
#include <boost/asio/post.hpp>

namespace odeum::relay {
namespace {
void send_packet(const std::shared_ptr<rtc::Track>& track, const rtc::binary& packet) {
    if (!track->isOpen()) return;
    try { track->send(packet); }
    catch (const std::exception&) { /* A concurrent transport close drops this packet; no payload is logged. */ }
}
void close_link(const std::shared_ptr<rtc::PeerConnection>& pc) { if (pc) { pc->resetCallbacks(); pc->close(); } }
// Marks the section removed before closing so the next relay offer rejects it (port 0).
void retire(const std::shared_ptr<rtc::Track>& track) {
    auto description = track->description(); description.markRemoved();
    try { track->setDescription(std::move(description)); } catch (const std::exception&) { /* already detached */ }
    track->close();
}
}
MediaRoom::MediaRoom(boost::asio::io_context& io, const Config& config) : io_(io), config_(config) {}
MediaRoom::~MediaRoom() { for (auto& [id, peer] : peers_) { close_link(peer.up.pc); close_link(peer.down.pc); } }
MediaRoom::Link MediaRoom::link(std::uint64_t id) {
    Link result{std::make_shared<rtc::PeerConnection>(config_.rtc)};
    auto weak = weak_from_this();
    result.pc->onLocalDescription([weak, id](rtc::Description description) {
        if (auto self = weak.lock()) boost::asio::post(self->io_, [weak, id, description = std::move(description)] {
            if (auto room = weak.lock(); room && room->peers_.contains(id)) {
                auto& peer = room->peers_.at(id);
                peer.send({{"type", "sdp"}, {"sdp", {{"type", description.typeString()}, {"sdp", std::string(description)}}}});
            }
        });
    });
    result.pc->onLocalCandidate([weak, id](rtc::Candidate candidate) {
        if (auto self = weak.lock()) boost::asio::post(self->io_, [weak, id, candidate = std::move(candidate)] {
            if (auto room = weak.lock(); room && room->peers_.contains(id)) room->peers_.at(id).send({{"type", "candidate"},
                {"candidate", public_candidate(std::string(candidate), room->config_.public_ip)}, {"mid", candidate.mid()}});
        });
    });
    return result;
}
void MediaRoom::join(std::uint64_t id, Role role, const std::string& slot, Send send) {
    Peer peer{role, slot, std::move(send)};
    const bool sends = role == Role::presenter || role == Role::producer, receives = role != Role::presenter;
    if (sends) peer.up = link(id);
    if (receives) peer.down = link(id);
    peers_.emplace(id, std::move(peer));
    if (receives) refresh(id);
}
void MediaRoom::leave(std::uint64_t id) {
    auto it = peers_.find(id); if (it == peers_.end()) return;
    close_link(it->second.up.pc); close_link(it->second.down.pc);
    const auto slot = it->second.slot; peers_.erase(it);
    if (auto source = sources_.find(slot); source != sources_.end() && source->second.peer == id) {
        sources_.erase(source); refresh_receivers();
    }
}
std::set<std::string> MediaRoom::wanted(const Peer& peer) const {
    std::set<std::string> live, result;
    for (const auto& [slot, source] : sources_) live.insert(slot);
    if (peer.role == Role::producer) {
        for (const auto& slot : live) if (slot != program_slot) result.insert(slot);
    } else if (auto slot = viewer_slot(live)) result.insert(*slot);
    return result;
}
void MediaRoom::refresh(std::uint64_t id) {
    auto& peer = peers_.at(id);
    const auto want = wanted(peer);
    Json closed = Json::array();
    for (auto it = peer.feeds.begin(); it != peer.feeds.end();) {
        auto source = sources_.find(it->second.slot);
        if (want.contains(it->second.slot) && source != sources_.end() && source->second.peer == it->second.source) { ++it; continue; }
        if (auto track = peer.down.tracks.find(it->first); track != peer.down.tracks.end()) { retire(track->second); peer.down.tracks.erase(track); }
        closed.push_back(it->first); it = peer.feeds.erase(it);
    }
    std::set<std::string> fed; for (const auto& [mid, feed] : peer.feeds) fed.insert(feed.slot);
    bool added = false;
    for (const auto& slot : want) {
        if (fed.contains(slot)) continue;
        auto& source = sources_.at(slot);
        for (const auto& [source_mid, media] : source.media) {
            const auto mid = slot + "-" + std::to_string(++peer.next_mid);
            auto track = peer.down.pc->addTrack(downstream_media(media, mid));
            peer.down.tracks.emplace(mid, track); peer.down_mids.insert(mid);
            peer.feeds.emplace(mid, Feed{slot, source.peer, source_mid});
            attach(id, false, mid, track);
        }
        source.keyframe_pending = true; added = true;
    }
    // The producer learns which sections ended before the offer that removes them.
    if (peer.role == Role::producer && !closed.empty()) peer.send({{"type", "track.closed"}, {"mids", closed}});
    if (added || !closed.empty()) negotiate(peer);
}
void MediaRoom::refresh_receivers() {
    for (auto& [id, peer] : peers_) if (peer.role != Role::presenter) refresh(id);
}
void MediaRoom::negotiate(Peer& peer) {
    // One offer at a time; a change during an outstanding offer is re-offered after the answer.
    if (peer.awaiting_answer) { peer.renegotiate = true; return; }
    peer.awaiting_answer = true; peer.down.pc->setLocalDescription(rtc::Description::Type::Offer);
}
void MediaRoom::request_keyframe(const std::string& slot) {
    if (auto source = sources_.find(slot); source != sources_.end()) source->second.keyframe_pending = true;
}
void MediaRoom::attach(std::uint64_t id, bool upstream, const std::string& mid, const std::shared_ptr<rtc::Track>& track) {
    auto weak = weak_from_this();
    track->onMessage([weak, id, upstream, mid](rtc::message_variant data) {
        auto packet = std::get_if<rtc::binary>(&data); if (!packet) return;
        if (auto room = weak.lock()) {
            if (room->queued_packets_.fetch_add(1) >= 512) { --room->queued_packets_; return; }
            boost::asio::post(room->io_, [weak, id, upstream, mid, packet = std::move(*packet)] {
                if (auto self = weak.lock()) { --self->queued_packets_; self->receive(id, upstream, mid, packet); }
            });
        }
    });
    track->onOpen([weak, id, upstream, mid] {
        if (auto room = weak.lock()) boost::asio::post(room->io_, [weak, id, upstream, mid] {
            auto self = weak.lock(); if (!self || !self->peers_.contains(id)) return;
            const auto& peer = self->peers_.at(id);
            if (upstream) self->request_keyframe(peer.slot);
            else if (auto feed = peer.feeds.find(mid); feed != peer.feeds.end()) self->request_keyframe(feed->second.slot);
        });
    });
}
void MediaRoom::publish(std::uint64_t id, const rtc::Description& offer) {
    auto& peer = peers_.at(id);
    auto description = offer;
    Source source{id};
    for (int i = 0; i < description.mediaCount(); ++i) {
        auto media = *std::get<rtc::Description::Media*>(description.media(i));
        // Keep only negotiated primary codecs; RTP remains byte-for-byte unchanged.
        for (auto pt : media.payloadTypes()) {
            if (!supported_codec(media, pt)) media.removeRtpMap(pt);
        }
        source.media.emplace(media.mid(), media);
        media.setDirection(rtc::Description::Direction::RecvOnly);
        auto track = peer.up.pc->addTrack(media); peer.up.tracks.emplace(media.mid(), track); attach(id, true, media.mid(), track);
    }
    peer.offered = true; peer.up.pc->setRemoteDescription(description); peer.up.pc->setLocalDescription(rtc::Description::Type::Answer);
    for (const auto& candidate : peer.up.candidates) peer.up.pc->addRemoteCandidate(candidate);
    peer.up.candidates.clear();
    sources_.insert_or_assign(peer.slot, std::move(source));
    refresh_receivers();
}
void MediaRoom::signal(std::uint64_t id, const Message& message) {
    auto& peer = peers_.at(id);
    if (message.type == MessageType::candidate) {
        auto value = message.body.at("candidate").get<std::string>(); if (value.empty()) return;
        const auto mid = message.body.at("mid").get<std::string>();
        // A producer's candidates belong to the relay offer when they name a relay-assigned mid.
        auto& link = peer.role == Role::viewer || (peer.role == Role::producer && peer.down_mids.contains(mid)) ? peer.down : peer.up;
        rtc::Candidate candidate(value, mid);
        if (link.pc->remoteDescription()) link.pc->addRemoteCandidate(candidate);
        else { if (link.candidates.size() >= 128) throw ProtocolError("capacity", "Too many ICE candidates"); link.candidates.push_back(candidate); }
        return;
    }
    const auto& body = message.body.at("sdp");
    rtc::Description description(body.at("sdp").get<std::string>(), body.at("type").get<std::string>());
    if (description.type() == rtc::Description::Type::Offer) {
        if (peer.role == Role::viewer) throw ProtocolError("invalid_sdp", "Viewer must answer the relay offer");
        if (peer.offered) throw ProtocolError("invalid_sdp", "Presenter must send one offer per connection");
        validate_offer(description);
        publish(id, description);
        return;
    }
    if (peer.role == Role::presenter) throw ProtocolError("invalid_sdp", "Presenter must send one offer per connection");
    if (description.type() != rtc::Description::Type::Answer || !peer.awaiting_answer)
        throw ProtocolError("invalid_sdp", "Viewer must answer the relay offer");
    std::set<std::string> live; for (const auto& [mid, track] : peer.down.tracks) live.insert(mid);
    validate_answer(description, peer.down_mids, live, peer.role == Role::producer ? 2 * config_.max_inputs : 2);
    peer.down.pc->setRemoteDescription(description);
    for (const auto& candidate : peer.down.candidates) peer.down.pc->addRemoteCandidate(candidate);
    peer.down.candidates.clear(); peer.awaiting_answer = false;
    for (const auto& [mid, feed] : peer.feeds) request_keyframe(feed.slot);
    if (peer.renegotiate) { peer.renegotiate = false; negotiate(peer); }
}
void MediaRoom::receive(std::uint64_t id, bool upstream, const std::string& mid, rtc::binary packet) {
    auto it = peers_.find(id); if (it == peers_.end()) return;
    if (upstream) {
        auto source = sources_.find(it->second.slot);
        if (source == sources_.end() || source->second.peer != id) return;
        for (const auto& [receiver_id, receiver] : peers_) {
            for (const auto& [down_mid, feed] : receiver.feeds) {
                if (feed.source != id || feed.source_mid != mid) continue;
                if (auto track = receiver.down.tracks.find(down_mid); track != receiver.down.tracks.end()) send_packet(track->second, packet);
            }
        }
    } else if (contains_keyframe_request(packet)) {
        // A viewer's request goes to its current source; the producer's to the slot of that section.
        if (auto feed = it->second.feeds.find(mid); feed != it->second.feeds.end()) request_keyframe(feed->second.slot);
    }
    // Receiver reports and NACK are intentionally terminated per receiver; PLI/FIR alone affect the source.
}
void MediaRoom::tick(Millis now) {
    for (auto& [slot, source] : sources_) {
        if (!source.keyframe_pending || now - source.last_keyframe < 1000) continue;
        const auto& sender = peers_.at(source.peer);
        bool open = true;
        for (const auto& [mid, description] : source.media) {
            if (description.type() != "video") continue;
            const auto& track = sender.up.tracks.at(mid);
            if (!track->isOpen()) { open = false; continue; }
            send_packet(track, picture_loss_indication(description.getSSRCs().at(0)));
        }
        if (open) { source.last_keyframe = now; source.keyframe_pending = false; }
    }
}
}
