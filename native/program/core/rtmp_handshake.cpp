#include "rtmp_handshake.hpp"
#include "bytes.hpp"
#include <algorithm>

namespace odeum::program {
RtmpHandshake::RtmpHandshake(std::uint32_t epoch_ms, const std::array<std::byte, rtmp_random_size>& random) {
    put_u32(c1_, epoch_ms);
    put_u32(c1_, 0);
    c1_.insert(c1_.end(), random.begin(), random.end());
}

std::vector<std::byte> RtmpHandshake::start() const {
    std::vector<std::byte> out;
    put_u8(out, rtmp_version);
    put_bytes(out, c1_);
    return out;
}

std::size_t RtmpHandshake::feed(std::span<const std::byte> data, std::vector<std::byte>& out) {
    std::size_t used = 0;
    while (used < data.size() && !done()) {
        if (received_ == 0) {
            if (get_u8(data, used) != rtmp_version) throw RtmpError("unsupported_version", "The server does not speak RTMP version 3");
            ++used;
            ++received_;
            continue;
        }
        if (received_ <= rtmp_handshake_size) {
            const auto take = std::min(data.size() - used, 1 + rtmp_handshake_size - received_);
            s1_.insert(s1_.end(), data.begin() + static_cast<std::ptrdiff_t>(used), data.begin() + static_cast<std::ptrdiff_t>(used + take));
            used += take;
            received_ += take;
            if (received_ == 1 + rtmp_handshake_size) put_bytes(out, s1_); // C2 echoes S1
            continue;
        }
        const auto take = std::min(data.size() - used, 1 + 2 * rtmp_handshake_size - received_);
        used += take;
        received_ += take;
    }
    return used;
}
}
