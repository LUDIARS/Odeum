#include "h264.hpp"
#include <lorica/checked_size.hpp>

namespace odeum::runner {
namespace {
constexpr std::size_t rtp_header_bytes = 12;
constexpr std::size_t frame_header_bytes = 13;
constexpr unsigned char fu_a_type = 28;
unsigned char byte(std::string_view s, std::size_t i) { return static_cast<unsigned char>(s[i]); }
}

std::vector<std::string_view> split_annex_b(std::string_view data) {
    std::vector<std::string_view> nals;
    auto push = [&](std::size_t start, std::size_t end) {
        while (end > start && data[end - 1] == '\0') --end;
        if (end > start) nals.push_back(data.substr(start, end - start));
    };
    std::size_t start = std::string_view::npos, i = 0;
    while (i + 2 < data.size()) {
        std::size_t code = 0;
        if (data[i] == '\0' && data[i + 1] == '\0') {
            if (byte(data, i + 2) == 1) code = 3;
            else if (i + 3 < data.size() && data[i + 2] == '\0' && byte(data, i + 3) == 1) code = 4;
        }
        if (code > 0) {
            if (start != std::string_view::npos) push(start, i);
            start = i + code;
            i += code;
        } else {
            ++i;
        }
    }
    if (start != std::string_view::npos) push(start, data.size());
    return nals;
}

H264RtpPacketizer::H264RtpPacketizer(int payload_type, std::size_t mtu, std::uint16_t initial_sequence, std::uint32_t ssrc)
    : payload_type_(payload_type), mtu_(mtu), sequence_(initial_sequence), ssrc_(ssrc) {}

std::string H264RtpPacketizer::packet(std::uint32_t timestamp, std::string_view prefix, std::string_view payload) {
    std::string out(rtp_header_bytes, '\0');
    out[0] = static_cast<char>(0x80);
    out[1] = static_cast<char>(payload_type_ & 0x7f);
    out[2] = static_cast<char>(sequence_ >> 8);
    out[3] = static_cast<char>(sequence_ & 0xff);
    for (int i = 0; i < 4; ++i) out[4 + i] = static_cast<char>((timestamp >> (24 - 8 * i)) & 0xff);
    for (int i = 0; i < 4; ++i) out[8 + i] = static_cast<char>((ssrc_ >> (24 - 8 * i)) & 0xff);
    sequence_ = static_cast<std::uint16_t>(sequence_ + 1);
    out.append(prefix);
    out.append(payload);
    return out;
}

std::vector<std::string> H264RtpPacketizer::packetize(std::string_view access_unit, std::int64_t timestamp_us) {
    // timestamp_us * 90 / 1000 without overflow (truncating like BigInt division), low 32 bits.
    const std::int64_t ticks = (timestamp_us / 1000) * 90 + ((timestamp_us % 1000) * 90) / 1000;
    const auto timestamp = static_cast<std::uint32_t>(static_cast<std::uint64_t>(ticks) & 0xffffffffu);
    const std::size_t payload_max = mtu_ - rtp_header_bytes;
    std::vector<std::string> packets;
    for (const auto nal : split_annex_b(access_unit)) {
        if (nal.size() <= payload_max) {
            packets.push_back(packet(timestamp, {}, nal));
            continue;
        }
        const char indicator = static_cast<char>((byte(nal, 0) & 0xe0) | fu_a_type);
        const unsigned type = byte(nal, 0) & 0x1f;
        const std::size_t chunk = payload_max - 2;
        for (std::size_t offset = 1; offset < nal.size(); offset += chunk) {
            const std::size_t end = std::min(nal.size(), offset + chunk);
            const char header = static_cast<char>((offset == 1 ? 0x80 : 0) | (end == nal.size() ? 0x40 : 0) | type);
            const char fu[2] = {indicator, header};
            packets.push_back(packet(timestamp, std::string_view(fu, 2), nal.substr(offset, end - offset)));
        }
    }
    if (!packets.empty()) packets.back()[1] = static_cast<char>(byte(packets.back(), 1) | 0x80);
    return packets;
}

std::vector<BridgeH264Frame> BridgeH264FrameReader::push(std::string_view chunk) {
    // At most one frame plus one read chunk is ever buffered (the length is checked first).
    const auto total = lorica::checked_add(buffer_.size(), chunk.size());
    if (!total) throw FrameTooLarge();
    buffer_.append(chunk);
    std::vector<BridgeH264Frame> frames;
    std::size_t offset = 0;
    const std::string_view view(buffer_);
    while (buffer_.size() - offset >= frame_header_bytes) {
        std::uint32_t length = 0;
        for (int i = 0; i < 4; ++i) length = (length << 8) | byte(view, offset + i);
        if (length > max_bridge_frame_bytes) throw FrameTooLarge();
        if (!lorica::contains_range(buffer_.size() - offset, frame_header_bytes, length)) break;
        if (length > 0) {
            std::uint64_t ts = 0;
            for (int i = 0; i < 8; ++i) ts = (ts << 8) | byte(view, offset + 4 + i);
            BridgeH264Frame frame;
            frame.data.assign(view.substr(offset + frame_header_bytes, length));
            frame.timestamp_us = static_cast<std::int64_t>(ts);
            frame.keyframe = (byte(view, offset + 12) & 1) == 1;
            frames.push_back(std::move(frame));
        }
        offset += frame_header_bytes + length;
    }
    buffer_.erase(0, offset);
    return frames;
}
}
