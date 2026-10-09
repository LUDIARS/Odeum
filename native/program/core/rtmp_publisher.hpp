#pragma once
#include "amf0.hpp"
#include "flv.hpp"
#include "rtmp_chunk.hpp"
#include "rtmp_handshake.hpp"
#include <string>

namespace odeum::program {
// Where to publish: rtmps://host:port/app and the stream key. The key is a secret; it only ever
// goes into the releaseStream / FCPublish / publish commands, never into errors or logs.
struct PublishTarget {
    std::string tc_url; // rtmps://a.rtmps.youtube.com:443/live2
    std::string app;    // live2
    std::string stream_key;
};

enum class PublishPhase { handshaking, connecting, creating_stream, publishing_requested, publishing, failed };

// The client side of an RTMP publish, as bytes in and bytes out, so the transport is a plain
// (TLS) pipe: handshake, Set Chunk Size 4096, connect(app, tcUrl) -> releaseStream, FCPublish,
// createStream -> publish(key, "live") -> NetStream.Publish.Start. Answers Window Ack Size,
// Set Peer Bandwidth and pings, and acknowledges received bytes per the server's window.
class RtmpPublisher {
public:
    static constexpr std::uint32_t chunk_size = 4096;
    RtmpPublisher(PublishTarget target, std::uint32_t epoch_ms, const std::array<std::byte, rtmp_random_size>& random);
    // C0 + C1.
    std::vector<std::byte> start();
    // Server bytes in; bytes to send back out. Throws RtmpError on a protocol failure or when
    // the server rejects the connection or the publish (code: connect_rejected / publish_rejected
    // / unsupported_version / invalid_chunk).
    std::vector<std::byte> receive(std::span<const std::byte> data);
    // Appends one tag as an audio / video / data message on the published stream. Only valid
    // while publishing().
    void send(const FlvTag& tag, std::vector<std::byte>& out) const;
    PublishPhase phase() const noexcept { return phase_; }
    bool publishing() const noexcept { return phase_ == PublishPhase::publishing; }
    std::uint32_t stream_id() const noexcept { return stream_id_; }
private:
    void handle(const RtmpMessage& message, std::vector<std::byte>& out);
    void command(std::uint8_t chunk_stream, std::uint32_t stream_id, const std::vector<Amf0Value>& values, std::vector<std::byte>& out);
    void after_connect(std::vector<std::byte>& out);
    PublishTarget target_;
    RtmpHandshake handshake_;
    ChunkWriter writer_;
    ChunkReader reader_;
    AckWindow ack_;
    PublishPhase phase_ = PublishPhase::handshaking;
    std::uint32_t stream_id_ = 0;
};
}
