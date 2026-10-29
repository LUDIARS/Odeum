#include "http_client.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <lorica/input_budget.hpp>
#include <algorithm>
#include <array>
#include <cctype>

namespace odeum::runner {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

std::string Url::origin() const {
    const bool v6 = host.find(':') != std::string::npos;
    return std::string(tls ? "https://" : "http://") + (v6 ? "[" + host + "]" : host) + ":" + port;
}

std::optional<Url> parse_url(std::string_view text) {
    Url url;
    std::string_view rest;
    if (text.substr(0, 7) == "http://") { rest = text.substr(7); url.tls = false; }
    else if (text.substr(0, 8) == "https://") { rest = text.substr(8); url.tls = true; }
    else return std::nullopt;
    const auto slash = rest.find_first_of("/?#");
    auto authority = rest.substr(0, slash);
    std::string target = slash == std::string_view::npos ? "/" : std::string(rest.substr(slash));
    if (const auto hash = target.find('#'); hash != std::string::npos) target.resize(hash);
    if (target.empty() || target.front() == '?') target.insert(target.begin(), '/');
    if (authority.empty() || authority.find('@') != std::string_view::npos) return std::nullopt;
    std::string_view port;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        url.host = std::string(authority.substr(1, close - 1));
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') return std::nullopt;
            port = authority.substr(close + 2);
        }
    } else {
        const auto colon = authority.rfind(':');
        url.host = std::string(authority.substr(0, colon));
        if (colon != std::string_view::npos) port = authority.substr(colon + 1);
    }
    if (url.host.empty()) return std::nullopt;
    for (char c : url.host) if (static_cast<unsigned char>(c) <= 0x20 || c == '/' || c == '\\') return std::nullopt;
    if (!port.empty()) {
        if (port.size() > 5 || !std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); })) return std::nullopt;
        const auto value = std::stoul(std::string(port));
        if (value == 0 || value > 65535) return std::nullopt;
        url.port = std::string(port);
    } else {
        url.port = url.tls ? "443" : "80";
    }
    for (char c : target) if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) return std::nullopt;
    url.target = std::move(target);
    return url;
}

namespace {
constexpr std::size_t header_limit = 16 * 1024;
const char* const timeout_message = "The operation was aborted due to timeout";
const char* const network_message = "fetch failed";

asio::ssl::context& tls_context() {
    static asio::ssl::context context = [] {
        asio::ssl::context c(asio::ssl::context::tls_client);
        c.set_default_verify_paths();
        c.set_verify_mode(asio::ssl::verify_peer);
        c.set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3 |
            asio::ssl::context::no_tlsv1 | asio::ssl::context::no_tlsv1_1);
        return c;
    }();
    return context;
}

std::string host_header(const Url& url) {
    const bool v6 = url.host.find(':') != std::string::npos;
    std::string host = v6 ? "[" + url.host + "]" : url.host;
    const bool default_port = (url.tls && url.port == "443") || (!url.tls && url.port == "80");
    return default_port ? host : host + ":" + url.port;
}

std::map<std::string, std::string> lower_headers(const http::response_header<>& header) {
    std::map<std::string, std::string> out;
    for (const auto& field : header) {
        std::string name(field.name_string());
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        out.emplace(std::move(name), std::string(field.value()));
    }
    return out;
}

template<class Stream>
class Exchange : public std::enable_shared_from_this<Exchange<Stream>> {
public:
    template<class... StreamArgs>
    Exchange(asio::io_context& io, Url url, HttpRequest request, std::function<void(HttpResult)> done, StreamArgs&&... args)
        : io_(io), resolver_(io), deadline_(io), stream_(std::forward<StreamArgs>(args)...), url_(std::move(url)), request_(std::move(request)),
          done_(std::move(done)), budget_({request_.body_limit, request_.body_limit, request_.body_limit, 1}) {}

    void start() {
        auto self = this->shared_from_this();
        deadline_.expires_after(request_.timeout);
        deadline_.async_wait([self](const boost::system::error_code& error) {
            if (error || self->finished_) return;
            self->timed_out_ = true;
            self->resolver_.cancel();
            beast::get_lowest_layer(self->stream_).cancel();
            beast::get_lowest_layer(self->stream_).close();
        });
        resolver_.async_resolve(url_.host, url_.port, [self](const boost::system::error_code& error, tcp::resolver::results_type results) {
            if (error) return self->fail(HttpFailure::unreachable);
            beast::get_lowest_layer(self->stream_).async_connect(results, [self](const boost::system::error_code& error, const tcp::endpoint&) {
                if (error) return self->fail(HttpFailure::unreachable);
                self->secure();
            });
        });
    }

private:
    asio::io_context& io_;
    tcp::resolver resolver_;
    asio::steady_timer deadline_;
    Stream stream_;
    Url url_;
    HttpRequest request_;
    std::function<void(HttpResult)> done_;
    lorica::input_budget budget_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> wire_;
    std::optional<http::response_parser<http::buffer_body>> parser_;
    std::array<char, 16384> chunk_{};
    HttpResponse response_;
    bool timed_out_ = false, finished_ = false;

    void secure() {
        if constexpr (std::is_same_v<Stream, beast::ssl_stream<beast::tcp_stream>>) {
            auto self = this->shared_from_this();
            if (!SSL_set_tlsext_host_name(stream_.native_handle(), url_.host.c_str())) return fail(HttpFailure::unreachable);
            stream_.set_verify_callback(asio::ssl::host_name_verification(url_.host));
            stream_.async_handshake(asio::ssl::stream_base::client, [self](const boost::system::error_code& error) {
                if (error) return self->fail(HttpFailure::unreachable);
                self->send();
            });
        } else {
            send();
        }
    }

    void send() {
        auto self = this->shared_from_this();
        auto verb = http::string_to_verb(request_.method);
        if (verb == http::verb::unknown) return fail(HttpFailure::protocol);
        wire_ = http::request<http::string_body>(verb, url_.target, 11);
        wire_.set(http::field::host, host_header(url_));
        wire_.set(http::field::user_agent, "odeum-runner");
        for (const auto& [name, value] : request_.headers) wire_.set(name, value);
        if (request_.body) wire_.body() = *request_.body;
        wire_.prepare_payload();
        wire_.keep_alive(false);
        http::async_write(stream_, wire_, [self](const boost::system::error_code& error, std::size_t) {
            if (error) return self->fail(HttpFailure::unreachable);
            self->parser_.emplace();
            self->parser_->header_limit(header_limit);
            self->parser_->body_limit(boost::none);  // charged against the Lorica budget below
            http::async_read_header(self->stream_, self->buffer_, *self->parser_, [self](const boost::system::error_code& error, std::size_t) {
                if (error) return self->fail(HttpFailure::unreachable);
                self->response_.status = self->parser_->get().result_int();
                self->response_.headers = lower_headers(self->parser_->get().base());
                self->read_body();
            });
        });
    }

    void read_body() {
        if (parser_->is_done()) return finish();
        auto self = this->shared_from_this();
        parser_->get().body().data = chunk_.data();
        parser_->get().body().size = chunk_.size();
        http::async_read(stream_, buffer_, *parser_, [self](boost::system::error_code error, std::size_t) {
            const std::size_t n = self->chunk_.size() - self->parser_->get().body().size;
            if (error == http::error::need_buffer) error = {};
            if (error) return self->fail(HttpFailure::unreachable);
            if (n > 0) {
                if (!self->budget_.consume(n, 0, 0)) return self->fail(HttpFailure::too_large);
                self->response_.body.append(self->chunk_.data(), n);
            }
            self->read_body();
        });
    }

    void finish() {
        if (finished_) return;
        finished_ = true;
        deadline_.cancel();
        boost::system::error_code ignored;
        beast::get_lowest_layer(stream_).socket().shutdown(tcp::socket::shutdown_both, ignored);
        beast::get_lowest_layer(stream_).close();
        auto done = std::move(done_);
        done(std::move(response_));
    }

    void fail(HttpFailure kind) {
        if (finished_) return;
        finished_ = true;
        deadline_.cancel();
        beast::get_lowest_layer(stream_).close();
        if (timed_out_) kind = HttpFailure::timeout;
        const char* message = kind == HttpFailure::timeout ? timeout_message
            : kind == HttpFailure::too_large ? "response body too large" : network_message;
        auto done = std::move(done_);
        done(HttpError{kind, message});
    }
};

class PlainStream final : public HttpStream {
public:
    PlainStream(asio::io_context& io, Url url, std::chrono::milliseconds timeout, std::size_t error_limit, Handlers handlers)
        : resolver_(io), stream_(io), timer_(io), url_(std::move(url)), timeout_(timeout), error_limit_(error_limit), handlers_(std::move(handlers)) {}

    void start() {
        auto self = std::static_pointer_cast<PlainStream>(shared_from_this());
        arm(timeout_);
        resolver_.async_resolve(url_.host, url_.port, [self](const boost::system::error_code& error, tcp::resolver::results_type results) {
            if (error) return self->end(HttpError{HttpFailure::unreachable, network_message});
            self->stream_.async_connect(results, [self](const boost::system::error_code& error, const tcp::endpoint&) {
                if (error) return self->end(HttpError{HttpFailure::unreachable, network_message});
                self->wire_ = http::request<http::empty_body>(http::verb::get, self->url_.target, 11);
                self->wire_.set(http::field::host, host_header(self->url_));
                self->wire_.set(http::field::user_agent, "odeum-runner");
                http::async_write(self->stream_, self->wire_, [self](const boost::system::error_code& error, std::size_t) {
                    if (error) return self->end(HttpError{HttpFailure::unreachable, network_message});
                    self->parser_.header_limit(header_limit);
                    self->parser_.body_limit(boost::none);
                    http::async_read_header(self->stream_, self->buffer_, self->parser_, [self](const boost::system::error_code& error, std::size_t) {
                        if (error) return self->end(HttpError{HttpFailure::unreachable, network_message});
                        const bool keep = self->handlers_.on_headers(self->parser_.get().result_int(), lower_headers(self->parser_.get().base()));
                        if (self->ended_) return;
                        self->collect_error_ = !keep;
                        if (keep) { self->timer_.cancel(); self->stream_.expires_never(); }
                        else self->arm(std::chrono::milliseconds(2000));
                        self->read();
                    });
                });
            });
        });
    }

    void cancel() override {
        if (ended_) return;
        ended_ = true;
        timer_.cancel();
        resolver_.cancel();
        stream_.close();
        auto end = std::move(handlers_.on_end);
        handlers_ = {};
        if (end) end(HttpError{HttpFailure::unreachable, "cancelled"});
    }
    const std::string& error_body() const override { return error_body_; }
    void pause() override { paused_ = true; }
    void resume() override {
        if (!paused_) return;
        paused_ = false;
        if (parked_ && !ended_) { parked_ = false; read(); }
    }

private:
    tcp::resolver resolver_;
    beast::tcp_stream stream_;
    asio::steady_timer timer_;
    Url url_;
    std::chrono::milliseconds timeout_;
    std::size_t error_limit_;
    Handlers handlers_;
    beast::flat_buffer buffer_;
    http::request<http::empty_body> wire_;
    http::response_parser<http::buffer_body> parser_;
    std::array<char, 65536> chunk_{};
    std::string error_body_;
    bool ended_ = false, collect_error_ = false, timed_out_ = false, paused_ = false, parked_ = false;

    void arm(std::chrono::milliseconds after) {
        auto self = std::static_pointer_cast<PlainStream>(shared_from_this());
        timer_.expires_after(after);
        timer_.async_wait([self](const boost::system::error_code& error) {
            if (error || self->ended_) return;
            self->timed_out_ = true;
            self->resolver_.cancel();
            self->stream_.close();
        });
    }

    void read() {
        if (parser_.is_done()) return end(std::nullopt);
        auto self = std::static_pointer_cast<PlainStream>(shared_from_this());
        parser_.get().body().data = chunk_.data();
        parser_.get().body().size = chunk_.size();
        http::async_read(stream_, buffer_, parser_, [self](boost::system::error_code error, std::size_t) {
            if (self->ended_) return;
            const std::size_t n = self->chunk_.size() - self->parser_.get().body().size;
            if (error == http::error::need_buffer) error = {};
            if (n > 0) {
                if (self->collect_error_) {
                    const auto room = self->error_limit_ > self->error_body_.size() ? self->error_limit_ - self->error_body_.size() : 0;
                    self->error_body_.append(self->chunk_.data(), std::min(room, n));
                    if (self->error_body_.size() >= self->error_limit_) return self->end(std::nullopt);
                } else if (!self->handlers_.on_data(std::string_view(self->chunk_.data(), n))) {
                    return self->cancel();
                }
                if (self->ended_) return;
            }
            if (error) return self->end(self->collect_error_ ? std::nullopt
                : std::optional<HttpError>(HttpError{HttpFailure::unreachable, error == http::error::end_of_stream ? "end of stream" : network_message}));
            if (self->paused_) { self->parked_ = true; return; }
            self->read();
        });
    }

    void end(std::optional<HttpError> error) {
        if (ended_) return;
        ended_ = true;
        timer_.cancel();
        stream_.close();
        if (error && timed_out_) error = HttpError{HttpFailure::timeout, timeout_message};
        auto done = std::move(handlers_.on_end);
        handlers_ = {};
        if (done) done(error);
    }
};
}

void http_request(asio::io_context& io, HttpRequest request, std::function<void(HttpResult)> done) {
    auto url = parse_url(request.url);
    if (!url) {
        asio::post(io, [done = std::move(done)] { done(HttpError{HttpFailure::invalid_url, "invalid URL"}); });
        return;
    }
    if (url->tls) {
        std::make_shared<Exchange<beast::ssl_stream<beast::tcp_stream>>>(io, *url, std::move(request), std::move(done), io, tls_context())->start();
    } else {
        std::make_shared<Exchange<beast::tcp_stream>>(io, *url, std::move(request), std::move(done), io)->start();
    }
}

std::shared_ptr<HttpStream> HttpStream::open(asio::io_context& io, const std::string& url_text, std::chrono::milliseconds header_timeout,
    std::size_t error_body_limit, Handlers handlers) {
    auto url = parse_url(url_text);
    if (!url || url->tls) {
        auto end = std::move(handlers.on_end);
        asio::post(io, [end] { if (end) end(HttpError{HttpFailure::invalid_url, "invalid URL"}); });
        return nullptr;
    }
    auto stream = std::make_shared<PlainStream>(io, *url, header_timeout, error_body_limit, std::move(handlers));
    stream->start();
    return stream;
}
}
