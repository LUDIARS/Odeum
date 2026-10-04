#pragma once
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace odeum::presenter {
inline constexpr int nal_sps = 7, nal_pps = 8, nal_idr = 5;

// Length-prefixed NAL units (AVCC, as VideoToolbox emits them) to Annex-B start codes.
// Throws std::invalid_argument when a length runs past the end or length_size is not 1, 2 or 4.
std::vector<std::byte> avcc_to_annexb(std::span<const std::byte> avcc, int length_size);

// Appends one NAL unit with a 4-byte start code.
void append_nal(std::vector<std::byte>& annexb, std::span<const std::byte> nal);

// True when the Annex-B stream holds a NAL unit of this type (3- or 4-byte start codes).
bool contains_nal(std::span<const std::byte> annexb, int type);

// Puts the parameter sets in front of a keyframe that lacks an SPS, so a viewer joining on any
// IDR can start decoding. Non-keyframes and keyframes that already carry an SPS are left alone.
void ensure_parameter_sets(std::vector<std::byte>& frame, bool keyframe, std::span<const std::byte> parameter_sets);
}
