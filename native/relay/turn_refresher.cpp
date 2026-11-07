#include "turn_refresher.hpp"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/write.hpp>
#include <functional>
#include <iostream>
#include <openssl/x509.h>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace odeum::relay {
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
namespace {
constexpr std::chrono::seconds request_timeout{15};

// One HTTPS POST over plain asio (HTTP/1.0, read until close); the exchange owns every buffer
// until the completion handler runs. A deadline timer closes the socket after 15 s.
struct Exchange : std::enable_shared_from_this<Exchange> {
    using Done = std::function<void(bool, const std::string&)>;
    tcp::resolver resolver;
    asio::ssl::stream<tcp::socket> stream;
    asio::steady_timer deadline;
    std::string request, response;
    Done done;
    Exchange(asio::io_context& io, asio::ssl::context& tls, Done callback)
        : resolver(io), stream(io, tls), deadline(io), done(std::move(callback)) {}
    void finish(bool ok, const std::string& body) {
        deadline.cancel();
        boost::system::error_code ignored; stream.lowest_layer().close(ignored);
        if (done) { auto callback = std::move(done); done = nullptr; callback(ok, body); }
    }
    void fail() { finish(false, {}); }
    void complete() {
        auto reply = parse_http_reply(response);
        if (!reply) return fail();
        finish(reply->status >= 200 && reply->status < 300, reply->body);
    }
    void run(const CloudflareTurn& turn) {
        const std::string host(cloudflare_turn_host);
        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) return fail();
        stream.set_verify_mode(asio::ssl::verify_peer);
        stream.set_verify_callback(asio::ssl::host_name_verification(host));
        request = cloudflare_http_request(turn);
        auto self = shared_from_this();
        deadline.expires_after(request_timeout);
        deadline.async_wait([self](const boost::system::error_code& ec) {
            if (!ec) { boost::system::error_code ignored; self->stream.lowest_layer().close(ignored); }
        });
        resolver.async_resolve(host, "443", [self](const boost::system::error_code& ec, const tcp::resolver::results_type& results) {
            if (ec) return self->fail();
            asio::async_connect(self->stream.lowest_layer(), results, [self](const boost::system::error_code& ec, const tcp::endpoint&) {
                if (ec) return self->fail();
                self->stream.async_handshake(asio::ssl::stream_base::client, [self](const boost::system::error_code& ec) {
                    if (ec) return self->fail();
                    asio::async_write(self->stream, asio::buffer(self->request), [self](const boost::system::error_code& ec, std::size_t) {
                        if (ec) return self->fail();
                        asio::async_read(self->stream, asio::dynamic_buffer(self->response, http_reply_limit),
                            [self](const boost::system::error_code& ec, std::size_t) {
                                // The server closes after the response; a missing TLS close_notify is tolerated
                                // because parse_http_reply checks the framing.
                                if (ec == asio::error::eof || ec == asio::ssl::error::stream_truncated) return self->complete();
                                self->fail();
                            });
                    });
                });
            });
        });
    }
};
}

void load_system_roots(asio::ssl::context& context) {
#ifdef _WIN32
    auto store = CertOpenSystemStoreW(0, L"ROOT");
    if (!store) throw std::runtime_error("Cannot open the system root store");
    auto* x509_store = SSL_CTX_get_cert_store(context.native_handle());
    for (PCCERT_CONTEXT cert = nullptr; (cert = CertEnumCertificatesInStore(store, cert)) != nullptr;) {
        const unsigned char* data = cert->pbCertEncoded;
        if (auto* x509 = d2i_X509(nullptr, &data, static_cast<long>(cert->cbCertEncoded))) {
            X509_STORE_add_cert(x509_store, x509); X509_free(x509);
        }
    }
    CertCloseStore(store, 0);
#else
    context.set_default_verify_paths();
#endif
}

TurnRefresher::TurnRefresher(asio::io_context& io, CloudflareTurn turn, std::shared_ptr<IceDirectory> directory)
    : io_(io), turn_(std::move(turn)), directory_(std::move(directory)), tls_(asio::ssl::context::tls_client), timer_(io) {
    tls_.set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2
        | asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1 | asio::ssl::context::no_tlsv1_1);
    load_system_roots(tls_);
}

void TurnRefresher::start() { fetch(); }

void TurnRefresher::fetch() {
    std::weak_ptr<TurnRefresher> weak = shared_from_this();
    auto exchange = std::make_shared<Exchange>(io_, tls_, [weak](bool ok, const std::string& body) {
        if (auto self = weak.lock()) self->finished(ok, body);
    });
    exchange->run(turn_);
}

void TurnRefresher::finished(bool ok, const std::string& body) {
    if (ok) {
        try {
            directory_->set_dynamic(parse_ice_servers(cloudflare_ice_servers(body)));
            failures_ = 0;
            std::cerr << "odeum-relay: TURN credentials refreshed\n";
            return schedule(cloudflare_refresh_delay(turn_));
        } catch (const std::exception&) { /* fall through: the message can quote the response */ }
    }
    ++failures_;
    std::cerr << "odeum-relay: TURN credential refresh failed; retrying\n";
    schedule(cloudflare_retry_delay(failures_));
}

void TurnRefresher::schedule(std::chrono::seconds delay) {
    timer_.expires_after(delay);
    std::weak_ptr<TurnRefresher> weak = shared_from_this();
    timer_.async_wait([weak](const boost::system::error_code& ec) { if (auto self = weak.lock(); self && !ec) self->fetch(); });
}
}
