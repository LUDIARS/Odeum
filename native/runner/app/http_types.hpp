#pragma once
// Transport-neutral request / response values shared by the server, router and handlers.
#include "../core/auth_policy.hpp"
#include "../core/json.hpp"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace odeum::runner {
struct ServerRequest {
    std::string method;   // upper case
    std::string target;   // as received
    std::string path;     // target without the query
    std::map<std::string, std::string> query;    // decoded, first value wins
    std::map<std::string, std::string> headers;  // lower-case names
    std::string body;
    std::optional<std::string> header(const std::string& lower_name) const {
        auto it = headers.find(lower_name);
        if (it == headers.end()) return std::nullopt;
        return it->second;
    }
};

struct ServerResponse {
    unsigned status = 200;
    std::string content_type;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    // Hono c.json(): application/json
    static ServerResponse json(unsigned status, const Json& body) { return {status, "application/json", dump(body), {}}; }
    static ServerResponse text(unsigned status, std::string body) { return {status, "text/plain; charset=UTF-8", std::move(body), {}}; }
    static ServerResponse not_found() { return text(404, "404 Not Found"); }
};

using Reply = std::function<void(ServerResponse)>;

class RequestHandler {
public:
    virtual ~RequestHandler() = default;
    // Calls reply exactly once, on the io_context thread.
    virtual void handle(const ServerRequest& request, Reply reply) = 0;
};

// The browser end of one WebSocket connection, as the broker sees it.
class WsPeer {
public:
    virtual ~WsPeer() = default;
    // droppable: a frame that may be skipped when the client reads too slowly (screenshots).
    virtual void send_text(std::string text, bool droppable) = 0;
    virtual void close(unsigned short code, const std::string& reason) = 0;
};

class WsEndpoint {
public:
    virtual ~WsEndpoint() = default;
    // ?token= first, else Authorization: Bearer. nullopt closes the socket with 4401.
    virtual void authorize(const ServerRequest& upgrade, std::function<void(std::optional<VerifiedUser>)> done) = 0;
    virtual std::string open(std::shared_ptr<WsPeer> peer, const VerifiedUser& user) = 0;
    virtual void message(const std::string& session_id, std::string_view text) = 0;
    virtual void closed(const std::string& session_id) = 0;
};

// Percent-decoding of one path segment or query component; nullopt for malformed escapes.
std::optional<std::string> percent_decode(std::string_view text, bool plus_is_space);
// URLSearchParams: '&'-separated, '+' as space; the first value of a name wins.
std::map<std::string, std::string> parse_query(std::string_view query);
// Path split on '/' with each segment percent-decoded; nullopt if any segment is malformed.
std::optional<std::vector<std::string>> path_segments(std::string_view path);
}
