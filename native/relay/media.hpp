#pragma once
#include "config.hpp"
#include "media_sdp.hpp"
#include "reactions.hpp"
#include <boost/asio/io_context.hpp>
#include <atomic>
#include <functional>
#include <map>
#include <set>

namespace odeum::relay {
using Send = std::function<void(const Json&)>;
// Per-room SFU. Each slot (input1..N, program) has at most one source. Viewers receive one source
// (program, else the lowest live input); the producer receives every live input and sends program.
// Downstream sections use relay-assigned mids "<slot>-<n>" so a receiver can tell slots apart.
class MediaRoom : public std::enable_shared_from_this<MediaRoom> {
public:
    MediaRoom(boost::asio::io_context& io, const Config& config);
    ~MediaRoom();
    // slot: the slot a presenter or producer sends to; empty for receive-only roles.
    void join(std::uint64_t id, Role role, const std::string& slot, Send send);
    void leave(std::uint64_t id);
    void signal(std::uint64_t id, const Message& message);
    void tick(Millis now);
private:
    // One PeerConnection. Senders own an upstream link, receivers a downstream link, the producer both.
    struct Link {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::map<std::string, std::shared_ptr<rtc::Track>> tracks;
        std::vector<rtc::Candidate> candidates;
    };
    // A downstream section: which slot's source (by sender id) and which source mid it carries.
    struct Feed { std::string slot; std::uint64_t source; std::string source_mid; };
    struct Peer {
        Role role;
        std::string slot;
        Send send;
        Link up, down;
        bool offered = false;          // upstream: the sender's single offer was accepted
        bool awaiting_answer = false;  // downstream: a relay offer is outstanding
        bool renegotiate = false;      // downstream: feeds changed while an offer was outstanding
        std::size_t next_mid = 0;
        std::set<std::string> down_mids;
        std::map<std::string, Feed> feeds;  // downstream mid -> feed
    };
    struct Source {
        std::uint64_t peer;
        std::map<std::string, rtc::Description::Media> media;  // source mid -> section
        Millis last_keyframe = -1000;
        bool keyframe_pending = true;
    };
    boost::asio::io_context& io_;
    const Config& config_;
    std::map<std::uint64_t, Peer> peers_;
    std::map<std::string, Source> sources_;
    std::atomic<int> queued_packets_{0};
    Link link(std::uint64_t id);
    std::set<std::string> wanted(const Peer& peer) const;
    void refresh(std::uint64_t id);
    void refresh_receivers();
    void negotiate(Peer& peer);
    void publish(std::uint64_t id, const rtc::Description& offer);
    void request_keyframe(const std::string& slot);
    void receive(std::uint64_t id, bool upstream, const std::string& mid, rtc::binary packet);
    void attach(std::uint64_t id, bool upstream, const std::string& mid, const std::shared_ptr<rtc::Track>& track);
};
}
