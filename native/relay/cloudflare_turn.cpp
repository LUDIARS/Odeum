#include "cloudflare_turn.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace odeum::relay {
namespace {
bool token_text(std::string_view value, std::size_t maximum) {
    return !value.empty() && value.size() <= maximum && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}
bool port_53(std::string_view url) {
    auto end = url.find('?'); auto host = url.substr(0, end);
    return host.ends_with(":53");
}
}

std::optional<CloudflareTurn> cloudflare_turn_from(const char* key_id, const char* api_token, const char* ttl) {
    if (!key_id && !api_token) {
        if (ttl) throw std::runtime_error("Cloudflare TURN TTL needs a key id and API token");
        return std::nullopt;
    }
    if (!key_id || !api_token) throw std::runtime_error("Cloudflare TURN needs both a key id and an API token");
    CloudflareTurn turn{key_id, api_token};
    if (!token_text(turn.key_id, 128)) throw std::runtime_error("Invalid Cloudflare TURN key id");
    if (!token_text(turn.api_token, 512)) throw std::runtime_error("Invalid Cloudflare TURN API token");
    if (ttl) {
        std::string_view text(ttl); long long seconds = 0;
        auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seconds);
        if (error != std::errc{} || end != text.data() + text.size()
            || seconds < cloudflare_turn_min_ttl.count() || seconds > cloudflare_turn_max_ttl.count()) throw std::runtime_error("Invalid Cloudflare TURN TTL");
        turn.ttl = std::chrono::seconds(seconds);
    }
    return turn;
}

std::string cloudflare_request_target(const CloudflareTurn& turn) {
    return "/v1/turn/keys/" + turn.key_id + "/credentials/generate-ice-servers";
}
std::string cloudflare_request_body(const CloudflareTurn& turn) {
    return Json{{"ttl", turn.ttl.count()}}.dump();
}

std::string cloudflare_http_request(const CloudflareTurn& turn) {
    auto body = cloudflare_request_body(turn);
    return "POST " + cloudflare_request_target(turn) + " HTTP/1.0\r\n"
        "Host: " + std::string(cloudflare_turn_host) + "\r\n"
        "Authorization: Bearer " + turn.api_token + "\r\n"
        "Content-Type: application/json\r\n"
        "User-Agent: odeum-relay\r\n"
        "Connection: close\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

namespace {
std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}
std::optional<std::string> dechunk(std::string_view data) {
    std::string out;
    while (true) {
        auto line_end = data.find("\r\n"); if (line_end == std::string_view::npos) return std::nullopt;
        auto size_line = data.substr(0, line_end);
        auto size_text = trim(size_line.substr(0, size_line.find(';')));
        std::size_t size = 0;
        auto [end, error] = std::from_chars(size_text.data(), size_text.data() + size_text.size(), size, 16);
        if (size_text.empty() || error != std::errc{} || end != size_text.data() + size_text.size()) return std::nullopt;
        data.remove_prefix(line_end + 2);
        if (size == 0) return out;
        if (size > http_reply_limit || data.size() < size + 2 || data.substr(size, 2) != "\r\n") return std::nullopt;
        out.append(data.substr(0, size)); data.remove_prefix(size + 2);
        if (out.size() > http_reply_limit) return std::nullopt;
    }
}
}

std::optional<HttpReply> parse_http_reply(std::string_view raw) {
    if (raw.size() > http_reply_limit) return std::nullopt;
    auto head_end = raw.find("\r\n\r\n"); if (head_end == std::string_view::npos) return std::nullopt;
    auto head = raw.substr(0, head_end); auto body = raw.substr(head_end + 4);
    auto status_end = head.find("\r\n"); auto status_line = head.substr(0, status_end);
    if (!status_line.starts_with("HTTP/1.") || status_line.size() < 12 || status_line[8] != ' ') return std::nullopt;
    HttpReply reply;
    auto code = status_line.substr(9, 3);
    auto [end, error] = std::from_chars(code.data(), code.data() + code.size(), reply.status);
    if (error != std::errc{} || end != code.data() + code.size() || reply.status < 100 || reply.status > 599) return std::nullopt;
    std::optional<std::size_t> length; bool chunked = false;
    for (auto rest = status_end == std::string_view::npos ? std::string_view{} : head.substr(status_end + 2); !rest.empty();) {
        auto line_end = rest.find("\r\n"); auto line = rest.substr(0, line_end);
        rest = line_end == std::string_view::npos ? std::string_view{} : rest.substr(line_end + 2);
        auto colon = line.find(':'); if (colon == std::string_view::npos) return std::nullopt;
        auto name = lower(trim(line.substr(0, colon))); auto value = trim(line.substr(colon + 1));
        if (name == "transfer-encoding") chunked = lower(value).find("chunked") != std::string::npos;
        if (name == "content-length") {
            std::size_t n = 0; auto [e, err] = std::from_chars(value.data(), value.data() + value.size(), n);
            if (err != std::errc{} || e != value.data() + value.size()) return std::nullopt;
            length = n;
        }
    }
    if (chunked) {
        auto decoded = dechunk(body); if (!decoded) return std::nullopt;
        reply.body = std::move(*decoded);
    } else if (length) {
        if (body.size() < *length) return std::nullopt;
        reply.body.assign(body.substr(0, *length));
    } else {
        reply.body.assign(body);
    }
    return reply;
}

Json cloudflare_ice_servers(std::string_view response_body) {
    auto document = Json::parse(response_body, nullptr, false);
    if (!document.is_object() || !document.contains("iceServers")) throw std::runtime_error("Unexpected Cloudflare TURN response");
    auto entries = document.at("iceServers");
    if (entries.is_object()) entries = Json::array({entries});
    if (!entries.is_array()) throw std::runtime_error("Unexpected Cloudflare TURN response");
    Json result = Json::array(); bool turn = false;
    for (const auto& entry : entries) {
        if (!entry.is_object() || !entry.contains("urls")) throw std::runtime_error("Unexpected Cloudflare TURN response");
        auto urls = entry.at("urls"); if (urls.is_string()) urls = Json::array({urls});
        if (!urls.is_array()) throw std::runtime_error("Unexpected Cloudflare TURN response");
        Json kept = Json::array(); bool relay = false;
        for (const auto& url : urls) {
            if (!url.is_string()) throw std::runtime_error("Unexpected Cloudflare TURN response");
            auto text = url.get<std::string>();
            if (port_53(text)) continue;
            relay = relay || text.starts_with("turn:") || text.starts_with("turns:");
            kept.push_back(text);
        }
        if (kept.empty()) continue;
        Json server = {{"urls", kept}};
        if (relay) {
            if (!entry.contains("username") || !entry.contains("credential") || !entry.at("username").is_string()
                || !entry.at("credential").is_string() || entry.at("username").get<std::string>().empty()
                || entry.at("credential").get<std::string>().empty()) throw std::runtime_error("Cloudflare TURN response lacks credentials");
            server["username"] = entry.at("username"); server["credential"] = entry.at("credential"); turn = true;
        }
        result.push_back(std::move(server));
    }
    if (!turn) throw std::runtime_error("Cloudflare TURN response has no TURN server");
    return result;
}

std::chrono::seconds cloudflare_refresh_delay(const CloudflareTurn& turn) { return turn.ttl / 2; }
std::chrono::seconds cloudflare_retry_delay(unsigned failures) {
    long long seconds = 5;
    for (unsigned i = 1; i < failures && seconds < 300; ++i) seconds *= 2;
    return std::chrono::seconds(std::min(seconds, 300LL));
}
}
