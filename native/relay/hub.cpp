#include "hub.hpp"

namespace odeum::relay {
Json Hub::presence(const Room& room) {
    return {{"type", "presence"}, {"presenter_connected", room.presenter != 0}, {"viewer_count", room.members.size() - (room.presenter ? 1 : 0)}};
}
void Hub::broadcast(const Room& room, const Json& message) { for (const auto& [id, member] : room.members) member.send(message); }
void Hub::join(std::uint64_t id, const Ticket& ticket, Send send, std::function<void()> close, Millis now, std::int64_t epoch) {
    if (ticket.role == Role::service) throw ProtocolError("forbidden", "Service tickets cannot join");
    if (!rooms_.contains(ticket.sid)) {
        if (rooms_.size() >= config_.max_sessions) throw ProtocolError("capacity", "Session limit reached");
        auto [it, inserted] = rooms_.try_emplace(ticket.sid, now);
        it->second.media = std::make_shared<MediaRoom>(io_, config_); it->second.started = epoch;
    }
    auto& room = rooms_.at(ticket.sid);
    if (ticket.role == Role::presenter && room.presenter) throw ProtocolError("presenter_exists", "Presenter already connected");
    if (ticket.role == Role::viewer && room.members.size() - (room.presenter ? 1 : 0) >= config_.max_viewers) throw ProtocolError("capacity", "Viewer limit reached");
    room.members.emplace(id, Participant{ticket, send, std::move(close)});
    if (ticket.role == Role::presenter) room.presenter = id;
    try { room.media->join(id, ticket.role, send); }
    catch (...) { room.members.erase(id); if (room.presenter == id) room.presenter = 0; if (room.members.empty()) rooms_.erase(ticket.sid); throw; }
    send({{"type", "welcome"}, {"sid", ticket.sid}, {"role", role_name(ticket.role)},
        {"self", {{"sub", ticket.sub}, {"name", ticket.name}}}, {"ice_servers", config_.ice_servers}});
    if (auto poll = room.poll.current()) send(*poll);
    broadcast(room, presence(room));
}
void Hub::leave(std::uint64_t id, const std::string& sid) {
    auto it = rooms_.find(sid); if (it == rooms_.end() || !it->second.members.contains(id)) return;
    auto& room = it->second; room.media->leave(id); room.members.erase(id);
    if (room.presenter == id) {
        room.presenter = 0; broadcast(room, presence(room));
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
    if (message.type == MessageType::sdp || message.type == MessageType::candidate) { room.media->signal(id, message); return; }
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
    if (message.type != MessageType::good && message.type != MessageType::stamp && message.type != MessageType::comment)
        throw ProtocolError("forbidden", "Server message cannot be sent by a client");
    auto result = room.reactions.accept(ticket.sub, message, now);
    if (result.limited) throw ProtocolError("rate_limited", "Reaction rate limit exceeded");
    if (message.type == MessageType::good || !room.presenter) return;
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
        {"viewer_count", room.members.size() - (room.presenter ? 1 : 0)}, {"started_at", room.started}});
    return result;
}
}
