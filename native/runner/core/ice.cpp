#include "ice.hpp"
#include <cctype>
#include <sstream>

namespace odeum::runner {
Json to_json(const IceServer& s) {
    Json out{{"urls", s.urls}};
    if (s.username) out["username"] = *s.username;
    if (s.credential) out["credential"] = *s.credential;
    return out;
}

Json to_json(const std::vector<IceServer>& servers) {
    Json out = Json::array();
    for (const auto& s : servers) out.push_back(to_json(s));
    return out;
}

std::vector<IceServer> stun_servers_from_env(const std::optional<std::string>& value) {
    const std::string raw = value ? *value : default_stun_url;
    IceServer server;
    std::stringstream in(raw);
    std::string url;
    while (std::getline(in, url, ',')) {
        const auto begin = url.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) continue;
        server.urls.push_back(url.substr(begin, url.find_last_not_of(" \t\r\n") - begin + 1));
    }
    if (server.urls.empty()) return {};
    return {server};
}

namespace {
// /:53(\?|$)/
bool port_53(const std::string& url) {
    for (std::size_t i = url.find(":53"); i != std::string::npos; i = url.find(":53", i + 1)) {
        if (i + 3 == url.size() || url[i + 3] == '?') return true;
    }
    return false;
}
}

std::vector<IceServer> parse_turn_response(const Json& body) {
    std::vector<IceServer> servers;
    if (!body.is_object()) return servers;
    auto list = body.find("iceServers");
    if (list == body.end() || list->is_null()) return servers;
    std::vector<Json> entries;
    if (list->is_array()) entries.assign(list->begin(), list->end());
    else entries.push_back(*list);
    for (const auto& entry : entries) {
        if (!entry.is_object()) continue;
        std::vector<Json> raw_urls;
        if (auto urls = entry.find("urls"); urls != entry.end()) {
            if (urls->is_array()) raw_urls.assign(urls->begin(), urls->end());
            else raw_urls.push_back(*urls);
        }
        IceServer server;
        for (const auto& url : raw_urls) {
            if (!url.is_string()) continue;
            const auto text = url.get<std::string>();
            if (text.rfind("turn", 0) != 0 || port_53(text)) continue;
            server.urls.push_back(text);
        }
        if (server.urls.empty()) continue;
        auto user = entry.find("username"), credential = entry.find("credential");
        if (user == entry.end() || !user->is_string() || credential == entry.end() || !credential->is_string()) continue;
        server.username = user->get<std::string>();
        server.credential = credential->get<std::string>();
        servers.push_back(std::move(server));
    }
    return servers;
}

std::vector<IceServer> flatten_for_peer(const std::vector<IceServer>& servers) {
    std::vector<IceServer> out;
    for (const auto& s : servers) {
        for (const auto& url : s.urls) {
            const bool stun = url.rfind("stun:", 0) == 0;
            const bool turn_udp = url.rfind("turn:", 0) == 0 && url.find("transport=tcp") == std::string::npos;
            if (!stun && !turn_udp) continue;
            IceServer one{{url}, std::nullopt, std::nullopt};
            if (s.username && !s.username->empty()) one.username = s.username;
            if (s.credential && !s.credential->empty()) one.credential = s.credential;
            out.push_back(std::move(one));
        }
    }
    return out;
}

std::string encode_uri_component(std::string_view text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')') out.push_back(static_cast<char>(c));
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}
}
