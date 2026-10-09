#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace odeum::program {
namespace rtmp_type {
inline constexpr std::uint8_t set_chunk_size = 1, abort = 2, acknowledgement = 3, user_control = 4, window_ack_size = 5,
    set_peer_bandwidth = 6, audio = 8, video = 9, data_amf0 = 18, command_amf0 = 20;
}

struct RtmpMessage {
    std::uint8_t type{};
    std::uint32_t timestamp{};
    std::uint32_t stream_id{};
    std::vector<std::byte> payload;
    bool operator==(const RtmpMessage&) const = default;
};

// Splits messages into chunks: a type-0 header on the first chunk, type-3 on the rest, extended
// timestamps from 0xFFFFFF. Chunk stream ids are 2..63 (one-byte basic header).
class ChunkWriter {
public:
    // The size announced to the peer with Set Chunk Size (1..0x7FFFFFFF).
    void chunk_size(std::uint32_t size);
    std::uint32_t chunk_size() const noexcept { return chunk_size_; }
    void write(std::uint8_t chunk_stream, const RtmpMessage& message, std::vector<std::byte>& out) const;
private:
    std::uint32_t chunk_size_ = 128;
};

// Reassembles the server's chunk stream (all four header types, 1- to 3-byte basic headers,
// extended timestamps). Set Chunk Size and Abort are applied here and also returned.
// Throws RtmpError("invalid_chunk") for a header that refers to an unknown chunk stream, a
// zero/oversized chunk size or a message over 16 MiB.
class ChunkReader {
public:
    std::vector<RtmpMessage> feed(std::span<const std::byte> data);
    std::uint32_t chunk_size() const noexcept { return chunk_size_; }
private:
    struct Stream {
        std::uint32_t timestamp{}, delta{}, length{}, stream_id{};
        std::uint8_t type{};
        bool extended{};
        std::vector<std::byte> partial;
    };
    // Parses one chunk from buffer_; false when more bytes are needed.
    bool chunk(std::vector<RtmpMessage>& out);
    std::vector<std::byte> buffer_;
    std::map<std::uint32_t, Stream> streams_;
    std::uint32_t chunk_size_ = 128;
};

// Acknowledgement bookkeeping: once the bytes received since the last acknowledgement reach the
// window the server set, returns the sequence number (total bytes received, modulo 2^32) to
// acknowledge.
class AckWindow {
public:
    void window(std::uint32_t size) noexcept { window_ = size; }
    std::uint32_t window() const noexcept { return window_; }
    std::optional<std::uint32_t> received(std::size_t bytes) noexcept;
private:
    std::uint32_t window_ = 0;
    std::uint64_t total_ = 0, acknowledged_ = 0;
};

// The control messages the client sends.
RtmpMessage set_chunk_size_message(std::uint32_t size);
RtmpMessage acknowledgement_message(std::uint32_t sequence);
RtmpMessage window_ack_size_message(std::uint32_t size);
// User Control "ping response" (event 7) echoing the server's timestamp.
RtmpMessage ping_response_message(std::uint32_t timestamp);
}
