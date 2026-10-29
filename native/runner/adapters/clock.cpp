#include "clock.hpp"
#include <openssl/rand.h>
#include <chrono>
#include <stdexcept>
#include <vector>

namespace odeum::runner {
long long epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

long long monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

namespace {
void fill(unsigned char* out, std::size_t n) {
    if (RAND_bytes(out, static_cast<int>(n)) != 1) throw std::runtime_error("random generator unavailable");
}
constexpr char hex_digits[] = "0123456789abcdef";
}

std::string random_hex(std::size_t bytes) {
    std::vector<unsigned char> raw(bytes);
    fill(raw.data(), raw.size());
    std::string out;
    out.reserve(bytes * 2);
    for (auto b : raw) { out.push_back(hex_digits[b >> 4]); out.push_back(hex_digits[b & 15]); }
    return out;
}

std::string random_uuid() {
    unsigned char b[16];
    fill(b, sizeof b);
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);
    std::string out;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(hex_digits[b[i] >> 4]);
        out.push_back(hex_digits[b[i] & 15]);
    }
    return out;
}

std::uint32_t random_below(std::uint32_t bound) {
    if (bound == 0) return 0;
    const std::uint32_t limit = UINT32_MAX - UINT32_MAX % bound;  // rejection sampling: no modulo bias
    for (;;) {
        std::uint32_t v = 0;
        fill(reinterpret_cast<unsigned char*>(&v), sizeof v);
        if (v < limit) return v % bound;
    }
}
}
