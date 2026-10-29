#pragma once
// Outbound HTTP/1.1 (http and https) with a whole-request deadline and a Lorica-charged response
// body limit. Redirects are not followed. TLS verifies the peer certificate and host name.
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace odeum::runner {
struct Url {
    bool tls = false;
    std::string host;  // without IPv6 brackets
    std::string port;
    std::string target;  // path + query, starting with '/'
    std::string origin() const;  // scheme://host[:port] as written for logs and errors
};
std::optional<Url> parse_url(std::string_view text);

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::optional<std::string> body;
    std::chrono::milliseconds timeout{10'000};
    std::size_t body_limit = 1024 * 1024;
};

struct HttpResponse {
    unsigned status = 0;
    std::map<std::string, std::string> headers;  // lower-case names, first value
    std::string body;
    bool ok() const { return status >= 200 && status < 300; }
};

enum class HttpFailure { invalid_url, unreachable, timeout, too_large, protocol };
struct HttpError {
    HttpFailure kind;
    std::string message;  // "fetch failed" / "The operation was aborted due to timeout" / ...
};
using HttpResult = std::variant<HttpResponse, HttpError>;

void http_request(boost::asio::io_context& io, HttpRequest request, std::function<void(HttpResult)> done);

// A long-lived plain-HTTP GET whose body is delivered in chunks (bridge video streams). The
// connect + header phase has `header_timeout`; the body has none (cancel() ends it).
class HttpStream : public std::enable_shared_from_this<HttpStream> {
public:
    struct Handlers {
        // Status and lower-case headers. Return false to stop (the stream is then cancelled).
        std::function<bool(unsigned, const std::map<std::string, std::string>&)> on_headers;
        // A body chunk; return false to stop.
        std::function<bool(std::string_view)> on_data;
        // Exactly once: nullopt at the normal end of the body, else why it stopped.
        std::function<void(const std::optional<HttpError>&)> on_end;
    };
    static std::shared_ptr<HttpStream> open(boost::asio::io_context& io, const std::string& url, std::chrono::milliseconds header_timeout,
        std::size_t error_body_limit, Handlers handlers);
    virtual ~HttpStream() = default;
    virtual void cancel() = 0;
    // The (non-200) response body read so far, up to the limit given to open().
    virtual const std::string& error_body() const = 0;
};
}
