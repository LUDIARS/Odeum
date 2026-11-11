#pragma once
#include <odeum/message.hpp>
#include <rtc/rtc.hpp>
#include <mutex>
#include <vector>

namespace odeum::relay {
// A validated ICE server list: the WebRTC form sent in welcome messages and the same servers in
// libdatachannel form for the relay's own peer connections.
struct IceSet {
    Json json = Json::array();
    std::vector<rtc::IceServer> servers;
};

// Validates a WebRTC-form list ({urls, username?, credential?}); TURN entries need credentials.
// Throws std::runtime_error without echoing values (they can contain credentials).
IceSet parse_ice_servers(const Json& list);

// The relay's current ICE servers: a fixed list from configuration plus a list that is replaced
// whenever short-lived TURN credentials are refreshed. Peer connections and welcomes created after
// a refresh use the new list; established connections keep their allocation.
class IceDirectory {
public:
    void set_static(IceSet set);
    void set_dynamic(IceSet set);
    bool has_dynamic() const;
    Json welcome() const;
    rtc::Configuration peer_configuration(const rtc::Configuration& base) const;
    // A new peer connection with `base` settings and the current ICE servers.
    std::shared_ptr<rtc::PeerConnection> open_peer(const rtc::Configuration& base) const;
private:
    mutable std::mutex mutex_;
    IceSet static_, dynamic_;
    bool has_dynamic_ = false;
};
}
