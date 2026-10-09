#include "tls_connection.hpp"
#include <openssl/err.h>
#include <openssl/ssl.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace odeum::program {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket no_socket = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
void blocking(Socket s, bool on) { u_long mode = on ? 0 : 1; ioctlsocket(s, FIONBIO, &mode); }
bool in_progress() { return WSAGetLastError() == WSAEWOULDBLOCK; }
void receive_timeout(Socket s, std::chrono::milliseconds t) {
    const DWORD ms = static_cast<DWORD>(t.count());
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof ms);
}
struct Winsock {
    Winsock() { WSADATA data; if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw TlsError("Winsock start failed"); }
    ~Winsock() { WSACleanup(); }
};
#else
using Socket = int;
constexpr Socket no_socket = -1;
void close_socket(Socket s) { ::close(s); }
void blocking(Socket s, bool on) {
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, on ? flags & ~O_NONBLOCK : flags | O_NONBLOCK);
}
bool in_progress() { return errno == EINPROGRESS; }
void receive_timeout(Socket s, std::chrono::milliseconds t) {
    timeval tv{static_cast<time_t>(t.count() / 1000), static_cast<suseconds_t>(t.count() % 1000 * 1000)};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}
struct Winsock {};
#endif

// Waits until the socket is readable (read) or writable (connect); false on timeout.
bool wait_for(Socket s, bool write, std::chrono::milliseconds wait) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{static_cast<long>(wait.count() / 1000), static_cast<long>(wait.count() % 1000 * 1000)};
    const auto ready = select(static_cast<int>(s + 1), write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv);
    if (ready < 0) throw TlsError("Socket wait failed");
    return ready > 0;
}

Socket connect_tcp(const std::string& host, int port, std::chrono::milliseconds timeout) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) throw TlsError("Cannot resolve the ingest host");
    Socket result = no_socket;
    for (auto* a = found; a && result == no_socket; a = a->ai_next) {
        Socket s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == no_socket) continue;
        blocking(s, false);
        const bool connected = ::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0 || (in_progress() && wait_for(s, true, timeout));
        int error = 0;
        socklen_t length = sizeof error;
        if (connected && getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == 0 && error == 0) {
            blocking(s, true);
            result = s;
        } else {
            close_socket(s);
        }
    }
    freeaddrinfo(found);
    if (result == no_socket) throw TlsError("Cannot connect to the ingest server");
    return result;
}
}

struct TlsConnection::State {
    Winsock winsock;
    SSL_CTX* context = nullptr;
    SSL* ssl = nullptr;
    Socket socket = no_socket;
    ~State() {
        if (ssl) SSL_free(ssl);
        if (context) SSL_CTX_free(context);
        if (socket != no_socket) close_socket(socket);
    }
};

TlsConnection::TlsConnection(const std::string& host, int port, std::chrono::milliseconds timeout) : state_(std::make_unique<State>()) {
    auto& s = *state_;
    s.socket = connect_tcp(host, port, timeout);
    receive_timeout(s.socket, timeout);
    s.context = SSL_CTX_new(TLS_client_method());
    if (!s.context) throw TlsError("Cannot create a TLS context");
    SSL_CTX_set_min_proto_version(s.context, TLS1_2_VERSION);
    SSL_CTX_set_verify(s.context, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_default_verify_paths(s.context);
#ifdef _WIN32
    // OpenSSL 3.2+ reads the Windows certificate store through this URI; older builds rely on
    // the default paths above.
    SSL_CTX_load_verify_store(s.context, "org.openssl.winstore://");
    ERR_clear_error();
#endif
    s.ssl = SSL_new(s.context);
    if (!s.ssl) throw TlsError("Cannot create a TLS session");
    SSL_set_tlsext_host_name(s.ssl, host.c_str());
    SSL_set1_host(s.ssl, host.c_str());
    SSL_set_fd(s.ssl, static_cast<int>(s.socket));
    if (SSL_connect(s.ssl) != 1) {
        const bool unverified = SSL_get_verify_result(s.ssl) != X509_V_OK;
        ERR_clear_error();
        throw TlsError(unverified ? "The ingest server's certificate could not be verified" : "TLS handshake failed");
    }
}

TlsConnection::~TlsConnection() { close(); }

void TlsConnection::write(std::span<const std::byte> data) {
    if (!state_) throw TlsError("Connection closed");
    std::size_t at = 0;
    while (at < data.size()) {
        std::size_t written = 0;
        if (SSL_write_ex(state_->ssl, data.data() + at, data.size() - at, &written) != 1) {
            ERR_clear_error();
            throw TlsError("TLS write failed");
        }
        at += written;
    }
}

std::size_t TlsConnection::read(std::span<std::byte> buffer, std::chrono::milliseconds wait) {
    if (!state_) throw TlsError("Connection closed");
    if (SSL_pending(state_->ssl) == 0 && !wait_for(state_->socket, false, wait)) return 0;
    std::size_t received = 0;
    if (SSL_read_ex(state_->ssl, buffer.data(), buffer.size(), &received) == 1) return received;
    const auto error = SSL_get_error(state_->ssl, 0);
    ERR_clear_error();
    // A record that carried no application data (a session ticket, a partial record).
    if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return 0;
    throw TlsError(error == SSL_ERROR_ZERO_RETURN ? "The ingest server closed the connection" : "TLS read failed");
}

void TlsConnection::close() noexcept {
    if (!state_) return;
    if (state_->ssl) SSL_shutdown(state_->ssl);
    state_.reset();
}
}
