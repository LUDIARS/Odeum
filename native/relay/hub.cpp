#include "hub.hpp"

namespace odeum::relay {
std::size_t Hub::count(const Room& room, Role role) {
    std::size_t n = 0; for (const auto& [id, member] : room.members) n += member.ticket.role == role; return n;
}
Json Hub::presence(const Room& room) {
    return {{"type", "presence"}, {"presenter_connected", room.presenter != 0}, {"viewer_count", count(room, Role::viewer)},
        {"reaction_version", room.reactions_ready ? 1 : 0}};
}
void Hub::broadcast(const Room& room, const Json& message) { for (const auto& [id, member] : room.members) member.send(message); }
void Hub::join(std::uint64_t id, const Ticket& ticket, Send send, std::function<void()> close, Millis now, std::int64_t epoch,
    Admission admission) {
    if (ticket.role == Role::service) throw ProtocolError("forbidden", "Service tickets cannot join");
    if ((admission == Admission::overlay) != (ticket.role == Role::overlay) || (admission == Admission::guest && ticket.role != Role::viewer))
        throw ProtocolError("forbidden", "Admission does not match role");
    if (!rooms_.contains(ticket.sid)) {
        if (rooms_.size() >= config_.max_sessions) throw ProtocolError("capacity", "Session limit reached");
        auto [it, inserted] = rooms_.try_emplace(ticket.sid, now);
        it->second.media = std::make_shared<MediaRoom>(io_, config_); it->second.started = epoch;
    }
    auto& room = rooms_.at(ticket.sid);
    auto discard_empty = [&] { if (room.members.empty()) rooms_.erase(ticket.sid); };
    if (ticket.role == Role::presenter && room.presenter) throw ProtocolError("presenter_exists", "Presenter already connected");
    if (ticket.role == Role::viewer && count(room, Role::viewer) >= config_.max_viewers) { discard_empty(); throw ProtocolError("capacity", "Viewer limit reached"); }
    if (ticket.role == Role::overlay && count(room, Role::overlay) >= max_overlays) throw ProtocolError("capacity", "Overlay limit reached");
    if (ticket.role == Role::presenter) {
        try { invitations_.attach(ticket.sid, ticket.invite_join, ticket.invite_overlay); }
        catch (...) { discard_empty(); throw; }
    }
    const bool media = admission == Admission::ticket;
    room.members.emplace(id, Participant{ticket, send, std::move(close), media});
    if (ticket.role == Role::presenter) room.presenter = id;
    if (media) {
        try { room.media->join(id, ticket.role, send); }
        catch (...) {
            room.members.erase(id);
            if (room.presenter == id) { room.presenter = 0; invitations_.detach(ticket.sid); }
            discard_empty(); throw;
        }
    }
    send({{"type", "welcome"}, {"sid", ticket.sid}, {"role", role_name(ticket.role)},
        {"self", {{"sub", ticket.sub}, {"name", ticket.name}}}, {"ice_servers", media ? config_.ice_servers : Json::array()}});
    if (auto poll = room.poll.current()) send(*poll);
    broadcast(room, presence(room));
}
void Hub::leave(std::uint64_t id, const std::string& sid) {
    auto it = rooms_.find(sid); if (it == rooms_.end() || !it->second.members.contains(id)) return;
    auto& room = it->second; room.media->leave(id); room.members.erase(id);
    if (room.presenter == id) {
        room.presenter = 0; invitations_.detach(sid); broadcast(room, presence(room));
        // Browser receivers reconnect with fresh tickets and SDP after source replacement.
        for (const auto& [viewer_id, member] : room.members) member.close();
        rooms_.erase(it); return;
    }
    if (room.members.empty()) rooms_.erase(it); else broadcast(room, presence(room));
}
void Hub::message(std::uint64_t id, const Ticket& ticket, const Message& message, Millis now, std::int64_t epoch) {
    auto it = rooms_.find(ticket.sid);
    if (it == rooms_.end() || !it->second.members.contains(id)) throw ProtocolError("session_closed", "Session closed");
    auto& room = it->second;
    if (message.type == MessageType::reaction_ready) {
        if (ticket.role != Role::presenter) throw ProtocolError("forbidden", "Presenter role required");
        room.reactions_ready = true;
        broadcast(room, presence(room));
        return;
    }
    if (ticket.role == Role::overlay) throw ProtocolError("forbidden", "Program overlays only receive");
    if (message.type == MessageType::sdp || message.type == MessageType::candidate) {
        if (!room.members.at(id).media) throw ProtocolError("forbidden", "Media is not available for this participant");
        room.media->signal(id, message); return;
    }
    if (message.type == MessageType::poll_open || message.type == MessageType::poll_close) {
        if (ticket.role != Role::presenter) throw ProtocolError("forbidden", "Presenter role required");
        if (message.type == MessageType::poll_open) { room.poll.open(message.body, now); broadcast(room, message.body); }
        else broadcast(room, room.poll.close(message.body.at("poll_id")));
        return;
    }
    if (ticket.role != Role::viewer) throw ProtocolError("forbidden", "Viewer role required");
    if (message.type == MessageType::poll_answer) {
        room.poll.answer(ticket.sub, message.body.at("poll_id"), message.body.at("choices").get<std::vector<int>>()); return;
    }
    const bool text_reaction = message.type == MessageType::telop || message.type == MessageType::submission;
    if (text_reaction && (!room.presenter || !room.reactions_ready))
        throw ProtocolError("reaction_unavailable", "An updated presenter must be connected");
    if (message.type != MessageType::good && message.type != MessageType::stamp && message.type != MessageType::comment && !text_reaction)
        throw ProtocolError("forbidden", "Server message cannot be sent by a client");
    auto result = room.reactions.accept(ticket.sub, message, now);
    if (result.limited) throw ProtocolError("rate_limited", "Reaction rate limit exceeded");
    if (message.type == MessageType::good || !room.presenter) return;
    if (text_reaction) {
        Json forwarded = {{"type", message.type == MessageType::telop ? "telop" : "submission"}, {"text", message.body.at("text")},
            {"from", {{"sub", ticket.sub}, {"name", ticket.name}}}, {"at", epoch}};
        if (message.type == MessageType::submission) {
            forwarded["category"] = message.body.at("category");
            forwarded["show_on_screen"] = message.body.at("show_on_screen");
        }
        // Private text never enters a viewer socket; identity is always server-derived.
        const bool public_text = message.type == MessageType::telop || message.body.at("show_on_screen").get<bool>();
        if (public_text) broadcast(room, forwarded);
        else room.members.at(room.presenter).send(forwarded);
        return;
    }
    Json forwarded = {{"type", message.type == MessageType::stamp ? "stamp" : "comment"},
        {"from", {{"sub", ticket.sub}, {"name", ticket.name}}}, {"at", epoch}};
    auto field = message.type == MessageType::stamp ? "kind" : "text"; forwarded[field] = message.body.at(field);
    room.members.at(room.presenter).send(forwarded);
}
void Hub::tick(Millis now) {
    for (auto& [sid, room] : rooms_) {
        if (auto burst = room.reactions.flush(now)) broadcast(room, *burst);
        if (auto tally = room.poll.flush(now)) broadcast(room, *tally);
        room.media->tick(now);
    }
}
Json Hub::sessions() const {
    Json result = Json::array();
    for (const auto& [sid, room] : rooms_) result.push_back({{"sid", sid}, {"presenter_connected", room.presenter != 0},
        {"viewer_count", count(room, Role::viewer)}, {"started_at", room.started}});
    return result;
}
}
