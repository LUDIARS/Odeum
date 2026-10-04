#include "h264_bitstream.hpp"

namespace odeum::presenter {
namespace {
constexpr std::byte start_code[] = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}};
}

std::vector<std::byte> avcc_to_annexb(std::span<const std::byte> avcc, int length_size) {
    if (length_size != 1 && length_size != 2 && length_size != 4) throw std::invalid_argument("Unsupported NAL length size");
    std::vector<std::byte> result;
    result.reserve(avcc.size() + 16);
    for (std::size_t offset = 0; offset < avcc.size();) {
        if (offset + static_cast<std::size_t>(length_size) > avcc.size()) throw std::invalid_argument("Truncated NAL length");
        std::size_t length = 0;
        for (int i = 0; i < length_size; ++i) length = (length << 8) | std::to_integer<std::size_t>(avcc[offset + static_cast<std::size_t>(i)]);
        offset += static_cast<std::size_t>(length_size);
        if (length == 0 || length > avcc.size() - offset) throw std::invalid_argument("NAL length runs past the sample");
        append_nal(result, avcc.subspan(offset, length));
        offset += length;
    }
    return result;
}

void append_nal(std::vector<std::byte>& annexb, std::span<const std::byte> nal) {
    annexb.insert(annexb.end(), std::begin(start_code), std::end(start_code));
    annexb.insert(annexb.end(), nal.begin(), nal.end());
}

bool contains_nal(std::span<const std::byte> annexb, int type) {
    for (std::size_t i = 0; i + 3 < annexb.size(); ++i) {
        if (annexb[i] != std::byte{0} || annexb[i + 1] != std::byte{0}) continue;
        std::size_t header = 0;
        if (annexb[i + 2] == std::byte{1}) header = i + 3;
        else if (annexb[i + 2] == std::byte{0} && i + 4 < annexb.size() && annexb[i + 3] == std::byte{1}) header = i + 4;
        else continue;
        if (header < annexb.size() && (std::to_integer<int>(annexb[header]) & 31) == type) return true;
        i = header - 1;
    }
    return false;
}

void ensure_parameter_sets(std::vector<std::byte>& frame, bool keyframe, std::span<const std::byte> parameter_sets) {
    if (!keyframe || parameter_sets.empty() || contains_nal(frame, nal_sps)) return;
    frame.insert(frame.begin(), parameter_sets.begin(), parameter_sets.end());
}
}
