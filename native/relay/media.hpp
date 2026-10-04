#pragma once
#include "config.hpp"
#include "reactions.hpp"
#include <boost/asio/io_context.hpp>
#include <atomic>
#include <functional>
#include <map>

namespace odeum::relay {
using Send = std::function<void(const Json&)>;
class MediaRoom : public std::enable_shared_from_this<MediaRoom> {
public:
    MediaRoom(boost::asio::io_context& io, const Config& config);
    ~MediaRoom();
    void join(std::uint64_t id, Role role, Send send);
    void leave(std::uint64_t id);
    void signal(std::uint64_t id, const Message& message);
    void tick(Millis now);
private:
    struct Peer {
        Role role;
        Send send;
        std::shared_ptr<rtc::PeerConnection> pc;
        std::map<std::string, std::shared_ptr<rtc::Track>> tracks;
        std::vector<rtc::Candidate> candidates;
        bool offered = false;
    };
    boost::asio::io_context& io_;
    const Config& config_;
    std::map<std::uint64_t, Peer> peers_;
    std::map<std::string, rtc::Description::Media> sources_;
    std::uint64_t presenter_ = 0;
    Millis last_keyframe_ = -1000;
    bool keyframe_pending_ = false;
    std::atomic<int> queued_packets_{0};
    void subscribe(std::uint64_t id);
    void receive(std::uint64_t id, const std::string& mid, rtc::binary packet);
    void attach(std::uint64_t id, const std::string& mid, const std::shared_ptr<rtc::Track>& track);
};
bool contains_keyframe_request(const rtc::binary& packet);
std::string public_candidate(std::string candidate, const std::string& address);
}
