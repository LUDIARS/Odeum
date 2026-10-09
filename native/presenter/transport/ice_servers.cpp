#include "ice_servers.hpp"
#include <rtc/rtc.hpp>

namespace odeum::presenter {
std::vector<rtc::IceServer> ice_servers(const Json& list) {
    std::vector<rtc::IceServer> result;
    if (!list.is_array()) return result;
    for (const auto& entry : list) {
        if (!entry.is_object() || !entry.contains("urls")) continue;
        std::vector<std::string> urls;
        if (entry["urls"].is_string()) urls.push_back(entry["urls"].get<std::string>());
        else if (entry["urls"].is_array())
            for (const auto& url : entry["urls"]) if (url.is_string()) urls.push_back(url.get<std::string>());
        for (const auto& url : urls) {
            try {
                rtc::IceServer server(url);
                if (server.type == rtc::IceServer::Type::Turn) {
                    if (entry.contains("username") && entry["username"].is_string()) server.username = entry["username"].get<std::string>();
                    if (entry.contains("credential") && entry["credential"].is_string()) server.password = entry["credential"].get<std::string>();
                }
                result.push_back(std::move(server));
            } catch (const std::exception&) {
                // An unreadable entry only loses that server; the relay's host candidates remain.
            }
        }
    }
    return result;
}
}
