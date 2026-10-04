#pragma once
#include "media.hpp"
#include "poll.hpp"
#include <odeum/ticket.hpp>

namespace odeum::relay {
class Hub {
public:
    Hub(boost::asio::io_context& io, const Config& config) : io_(io), config_(config) {}
    void join(std::uint64_t id, const Ticket& ticket, Send send, std::function<void()> close, Millis now, std::int64_t epoch);
    void leave(std::uint64_t id, const std::string& sid);
    void message(std::uint64_t id, const Ticket& ticket, const Message& message, Millis now, std::int64_t epoch);
    void tick(Millis now);
    Json sessions() const;
private:
    struct Participant { Ticket ticket; Send send; std::function<void()> close; };
    struct Room {
        explicit Room(Millis now) : reactions(now) {}
        std::map<std::uint64_t, Participant> members;
        std::shared_ptr<MediaRoom> media;
        Reactions reactions;
        Poll poll;
        std::int64_t started = 0;
        std::uint64_t presenter = 0;
    };
    boost::asio::io_context& io_;
    const Config& config_;
    std::map<std::string, Room> rooms_;
    static void broadcast(const Room& room, const Json& message);
    static Json presence(const Room& room);
};
}
