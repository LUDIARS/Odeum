#include "rtmp_chunk.hpp"
#include "bytes.hpp"
#include "rtmp_handshake.hpp"
#include <algorithm>

namespace odeum::program {
namespace {
constexpr std::uint32_t extended_marker = 0xFFFFFF;
constexpr std::uint32_t max_message_bytes = 16u << 20;
constexpr std::uint32_t max_chunk_size = 0x7FFFFFFF;

RtmpMessage control(std::uint8_t type, std::uint32_t value) {
    RtmpMessage message{type, 0, 0, {}};
    put_u32(message.payload, value);
    return message;
}
}

void ChunkWriter::chunk_size(std::uint32_t size) {
    if (size == 0 || size > max_chunk_size) throw RtmpError("invalid_chunk", "Chunk size out of range");
    chunk_size_ = size;
}

void ChunkWriter::write(std::uint8_t chunk_stream, const RtmpMessage& message, std::vector<std::byte>& out) const {
    if (chunk_stream < 2 || chunk_stream > 63) throw RtmpError("invalid_chunk", "Chunk stream id must be 2..63");
    if (message.payload.size() > max_message_bytes) throw RtmpError("invalid_chunk", "Message too large");
    const bool extended = message.timestamp >= extended_marker;
    put_u8(out, chunk_stream); // fmt 0
    put_u24(out, extended ? extended_marker : message.timestamp);
    put_u24(out, static_cast<std::uint32_t>(message.payload.size()));
    put_u8(out, message.type);
    put_u32_le(out, message.stream_id);
    if (extended) put_u32(out, message.timestamp);
    std::size_t at = 0;
    do {
        if (at > 0) {
            put_u8(out, 0xC0u | chunk_stream); // fmt 3
            if (extended) put_u32(out, message.timestamp);
        }
        const auto take = std::min<std::size_t>(chunk_size_, message.payload.size() - at);
        out.insert(out.end(), message.payload.begin() + static_cast<std::ptrdiff_t>(at), message.payload.begin() + static_cast<std::ptrdiff_t>(at + take));
        at += take;
    } while (at < message.payload.size());
}

std::vector<RtmpMessage> ChunkReader::feed(std::span<const std::byte> data) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
    std::vector<RtmpMessage> out;
    while (chunk(out)) {}
    return out;
}

bool ChunkReader::chunk(std::vector<RtmpMessage>& out) {
    const std::span<const std::byte> in(buffer_);
    if (in.empty()) return false;
    const auto first = get_u8(in, 0);
    const auto fmt = first >> 6;
    std::uint32_t id = first & 0x3f;
    std::size_t at = 1;
    if (id == 0) { if (in.size() < 2) return false; id = 64 + get_u8(in, 1); at = 2; }
    else if (id == 1) { if (in.size() < 3) return false; id = 64 + get_u8(in, 1) + get_u8(in, 2) * 256; at = 3; }
    constexpr std::size_t header_sizes[] = {11, 7, 3, 0};
    if (in.size() < at + header_sizes[fmt]) return false;
    const auto known = streams_.find(id);
    if (fmt != 0 && known == streams_.end()) throw RtmpError("invalid_chunk", "Chunk refers to an unknown chunk stream");
    Stream next = known == streams_.end() ? Stream{} : known->second;
    const bool starts = next.partial.empty();
    std::uint32_t field = 0;
    if (fmt <= 2) field = get_u24(in, at);
    if (fmt <= 1) {
        next.length = get_u24(in, at + 3);
        next.type = static_cast<std::uint8_t>(get_u8(in, at + 6));
        if (next.length > max_message_bytes) throw RtmpError("invalid_chunk", "Message too large");
    }
    if (fmt == 0) next.stream_id = get_u32_le(in, at + 7);
    at += header_sizes[fmt];
    if (fmt <= 2) next.extended = field == extended_marker;
    if (next.extended) {
        if (in.size() < at + 4) return false;
        if (fmt <= 2) field = get_u32(in, at);
        else if (starts) field = get_u32(in, at); // type 3 repeats the extended value
        at += 4;
    }
    if (starts) {
        if (fmt == 0) next.timestamp = field;
        else if (fmt <= 2) { next.delta = field; next.timestamp += field; }
        else next.timestamp += next.delta;
    }
    const auto take = std::min<std::size_t>(chunk_size_, next.length - next.partial.size());
    if (in.size() < at + take) return false;
    next.partial.insert(next.partial.end(), in.begin() + static_cast<std::ptrdiff_t>(at), in.begin() + static_cast<std::ptrdiff_t>(at + take));
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(at + take));
    if (next.partial.size() == next.length) {
        RtmpMessage message{next.type, next.timestamp, next.stream_id, std::move(next.partial)};
        next.partial.clear();
        if (message.type == rtmp_type::set_chunk_size) {
            if (message.payload.size() < 4) throw RtmpError("invalid_chunk", "Short Set Chunk Size");
            const auto size = get_u32(message.payload, 0) & 0x7FFFFFFF;
            if (size == 0) throw RtmpError("invalid_chunk", "Zero chunk size");
            chunk_size_ = size;
        } else if (message.type == rtmp_type::abort && message.payload.size() >= 4) {
            if (auto aborted = streams_.find(get_u32(message.payload, 0)); aborted != streams_.end()) aborted->second.partial.clear();
        }
        out.push_back(std::move(message));
    }
    streams_[id] = std::move(next);
    return true;
}

std::optional<std::uint32_t> AckWindow::received(std::size_t bytes) noexcept {
    total_ += bytes;
    if (window_ == 0 || total_ - acknowledged_ < window_) return std::nullopt;
    acknowledged_ = total_;
    return static_cast<std::uint32_t>(total_ & 0xffffffffu);
}

RtmpMessage set_chunk_size_message(std::uint32_t size) { return control(rtmp_type::set_chunk_size, size); }
RtmpMessage acknowledgement_message(std::uint32_t sequence) { return control(rtmp_type::acknowledgement, sequence); }
RtmpMessage window_ack_size_message(std::uint32_t size) { return control(rtmp_type::window_ack_size, size); }

RtmpMessage ping_response_message(std::uint32_t timestamp) {
    RtmpMessage message{rtmp_type::user_control, 0, 0, {}};
    put_u16(message.payload, 7);
    put_u32(message.payload, timestamp);
    return message;
}
}
