#pragma once
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace odeum::program {
// A failure of the TLS connection. The message names the step, never data that was sent.
struct TlsError : std::runtime_error { using runtime_error::runtime_error; };

// A blocking TCP + TLS (OpenSSL 3) client connection with certificate and host-name
// verification against the system trust store. One thread uses it at a time.
class TlsConnection {
public:
    // Resolves, connects and completes the TLS handshake with SNI = host. Throws TlsError.
    TlsConnection(const std::string& host, int port, std::chrono::milliseconds timeout);
    ~TlsConnection();
    TlsConnection(const TlsConnection&) = delete;
    TlsConnection& operator=(const TlsConnection&) = delete;
    // Writes everything or throws TlsError.
    void write(std::span<const std::byte> data);
    // Up to buffer.size() bytes; 0 when nothing arrived within `wait`. Throws TlsError when the
    // peer closed the connection or it failed.
    std::size_t read(std::span<std::byte> buffer, std::chrono::milliseconds wait);
    void close() noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
