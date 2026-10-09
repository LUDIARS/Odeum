#pragma once
#include <odeum/message.hpp>
#include <vector>

namespace rtc { struct IceServer; }

namespace odeum::presenter {
// The welcome's ice_servers ({urls, username?, credential?}, WebRTC form) as libdatachannel
// servers. An unreadable entry only loses that server; the relay's host candidates remain.
std::vector<rtc::IceServer> ice_servers(const Json& list);
}
