#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace odeum::program {
// Big-endian writers and readers shared by the FLV, AMF0 and RTMP code.
inline void put_u8(std::vector<std::byte>& out, std::uint32_t v) { out.push_back(static_cast<std::byte>(v & 0xff)); }
inline void put_u16(std::vector<std::byte>& out, std::uint32_t v) { put_u8(out, v >> 8); put_u8(out, v); }
inline void put_u24(std::vector<std::byte>& out, std::uint32_t v) { put_u8(out, v >> 16); put_u16(out, v); }
inline void put_u32(std::vector<std::byte>& out, std::uint32_t v) { put_u16(out, v >> 16); put_u16(out, v); }
// RTMP's message stream id is the one little-endian field.
inline void put_u32_le(std::vector<std::byte>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) put_u8(out, v >> (8 * i));
}
inline void put_f64(std::vector<std::byte>& out, double v) {
    const auto bits = std::bit_cast<std::uint64_t>(v);
    put_u32(out, static_cast<std::uint32_t>(bits >> 32));
    put_u32(out, static_cast<std::uint32_t>(bits));
}
inline void put_bytes(std::vector<std::byte>& out, std::span<const std::byte> data) { out.insert(out.end(), data.begin(), data.end()); }
inline void put_text(std::vector<std::byte>& out, std::string_view text) {
    const auto* p = reinterpret_cast<const std::byte*>(text.data());
    out.insert(out.end(), p, p + text.size());
}

inline std::uint32_t get_u8(std::span<const std::byte> in, std::size_t at) { return std::to_integer<std::uint32_t>(in[at]); }
inline std::uint32_t get_u16(std::span<const std::byte> in, std::size_t at) { return get_u8(in, at) << 8 | get_u8(in, at + 1); }
inline std::uint32_t get_u24(std::span<const std::byte> in, std::size_t at) { return get_u8(in, at) << 16 | get_u16(in, at + 1); }
inline std::uint32_t get_u32(std::span<const std::byte> in, std::size_t at) { return get_u16(in, at) << 16 | get_u16(in, at + 2); }
inline std::uint32_t get_u32_le(std::span<const std::byte> in, std::size_t at) {
    return get_u8(in, at) | get_u8(in, at + 1) << 8 | get_u8(in, at + 2) << 16 | get_u8(in, at + 3) << 24;
}
}
