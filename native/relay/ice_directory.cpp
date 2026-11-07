#include "ice_directory.hpp"
#include <stdexcept>

namespace odeum::relay {
IceSet parse_ice_servers(const Json& list) {
    if (!list.is_array() || list.size() > 16) throw std::runtime_error("ICE servers must be an array of at most 16 entries");
    IceSet set; set.json = list;
    for (const auto& server : list) {
        if (!server.is_object() || !server.contains("urls")) throw std::runtime_error("Invalid ICE server");
        auto urls = server.at("urls"); if (urls.is_string()) urls = Json::array({urls});
        if (!urls.is_array() || urls.empty() || urls.size() > 16) throw std::runtime_error("Invalid ICE URLs");
        for (const auto& url : urls) {
            if (!url.is_string()) throw std::runtime_error("Invalid ICE URL");
            auto text = url.get<std::string>();
            if (!(text.starts_with("stun:") || text.starts_with("turn:") || text.starts_with("turns:"))) throw std::runtime_error("Invalid ICE scheme");
            rtc::IceServer ice(text);
            if (ice.type == rtc::IceServer::Type::Turn) {
                if (!server.contains("username") || !server.contains("credential")
                    || !server.at("username").is_string() || !server.at("credential").is_string()) throw std::runtime_error("TURN credentials required");
                ice.username = server.at("username").get<std::string>(); ice.password = server.at("credential").get<std::string>();
                if (ice.username.empty() || ice.password.empty()) throw std::runtime_error("TURN credentials required");
            }
            set.servers.push_back(std::move(ice));
        }
    }
    return set;
}

void IceDirectory::set_static(IceSet set) { std::lock_guard lock(mutex_); static_ = std::move(set); }
void IceDirectory::set_dynamic(IceSet set) { std::lock_guard lock(mutex_); dynamic_ = std::move(set); has_dynamic_ = true; }
bool IceDirectory::has_dynamic() const { std::lock_guard lock(mutex_); return has_dynamic_; }
Json IceDirectory::welcome() const {
    std::lock_guard lock(mutex_);
    Json list = static_.json;
    for (const auto& server : dynamic_.json) list.push_back(server);
    return list;
}
rtc::Configuration IceDirectory::peer_configuration(const rtc::Configuration& base) const {
    std::lock_guard lock(mutex_);
    rtc::Configuration config = base;
    config.iceServers = static_.servers;
    config.iceServers.insert(config.iceServers.end(), dynamic_.servers.begin(), dynamic_.servers.end());
    return config;
}
std::shared_ptr<rtc::PeerConnection> IceDirectory::open_peer(const rtc::Configuration& base) const {
    return std::make_shared<rtc::PeerConnection>(peer_configuration(base));
}
}
