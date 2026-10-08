#pragma once
#include "media.hpp"
#include "invitations.hpp"
#include "poll.hpp"
#include <odeum/ticket.hpp>

namespace odeum::relay {
// How a participant was admitted. Guests are viewers without media; overlays only receive broadcasts.
enum class Admission { ticket, guest, overlay };
class Hub {
public:
    static constexpr std::size_t max_overlays = 4;
    Hub(boost::asio::io_context& io, const Config& config) : io_(io), config_(config) {}
    void join(std::uint64_t id, const Ticket& ticket, Send send, std::function<void()> close, Millis now, std::int64_t epoch,
        Admission admission = Admission::ticket);
    Invitations& invitations() { return invitations_; }
    void leave(std::uint64_t id, const std::string& sid);
    void message(std::uint64_t id, const Ticket& ticket, const Message& message, Millis now, std::int64_t epoch);
    void tick(Millis now);
    Json sessions() const;
private:
    struct Participant { Ticket ticket; Send send; std::function<void()> close; bool media; };
    struct Room {
        explicit Room(Millis now) : reactions(now) {}
        std::map<std::uint64_t, Participant> members;
        std::shared_ptr<MediaRoom> media;
        Reactions reactions;
        Poll poll;
        std::int64_t started = 0;
        std::uint64_t presenter = 0;
        bool reactions_ready = false;
    };
    boost::asio::io_context& io_;
    const Config& config_;
    std::map<std::string, Room> rooms_;
    Invitations invitations_;
    static std::size_t count(const Room& room, Role role);
    static void broadcast(const Room& room, const Json& message);
    static Json presence(const Room& room);
};
}
