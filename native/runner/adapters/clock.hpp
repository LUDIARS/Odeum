#pragma once
// Wall clock, monotonic clock and random identifiers (OpenSSL RAND_bytes).
#include <cstdint>
#include <string>

namespace odeum::runner {
long long epoch_ms();
long long monotonic_ms();
// RFC 4122 version 4 UUID, lower-case hex (crypto.randomUUID()).
std::string random_uuid();
// Uniform integer in [0, bound) from the OpenSSL generator.
std::uint32_t random_below(std::uint32_t bound);
// n random bytes as lower-case hex.
std::string random_hex(std::size_t bytes);
}
