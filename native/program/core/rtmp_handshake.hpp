#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace odeum::program {
inline constexpr std::size_t rtmp_handshake_size = 1536;
inline constexpr std::size_t rtmp_random_size = rtmp_handshake_size - 8;
inline constexpr std::uint8_t rtmp_version = 3;

// A protocol failure. `code` is a fixed word (never server text that could echo the stream key).
struct RtmpError : std::runtime_error {
    std::string code;
    RtmpError(std::string c, const std::string& message) : runtime_error(message), code(std::move(c)) {}
};

// The client side of the simple RTMP handshake: C0+C1 out, S0+S1+S2 in, C2 (an echo of S1) out.
// S2 is read but not compared with C1; the digest handshake is not used.
class RtmpHandshake {
public:
    RtmpHandshake(std::uint32_t epoch_ms, const std::array<std::byte, rtmp_random_size>& random);
    // C0 + C1 (1537 bytes).
    std::vector<std::byte> start() const;
    // Consumes server bytes up to the end of S2 and appends C2 to `out` once S1 is complete.
    // Returns how many bytes it consumed; what follows S2 already belongs to the chunk stream.
    // Throws RtmpError("unsupported_version") when S0 is not 3.
    std::size_t feed(std::span<const std::byte> data, std::vector<std::byte>& out);
    bool done() const noexcept { return received_ == 1 + 2 * rtmp_handshake_size; }
private:
    std::vector<std::byte> c1_, s1_;
    std::size_t received_ = 0;
};
}
