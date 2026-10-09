#include "rtmp_publisher.hpp"
#include "amf0.hpp"
#include "bytes.hpp"

namespace odeum::program {
namespace {
// Chunk streams: 2 protocol control, 3 commands on stream 0, 4 audio, 5 data, 6 video, 8 stream commands.
constexpr std::uint8_t control_stream = 2, command_stream = 3, audio_stream = 4, data_stream = 5, video_stream = 6, stream_command_stream = 8;
constexpr double connect_transaction = 1, release_transaction = 2, fcpublish_transaction = 3, create_transaction = 4, publish_transaction = 5;
constexpr std::uint32_t client_window = 2500000;

std::string text_of(const Amf0Value* value) { return value && value->type == Amf0Value::Type::string ? value->string : std::string{}; }

// "status" level / fixed code of an onStatus or _error info object; never the description, which
// may quote the stream name.
std::string status_code(const std::vector<Amf0Value>& values) {
    for (const auto& value : values) if (value.type == Amf0Value::Type::object) return text_of(value.find("code"));
    return {};
}
}

RtmpPublisher::RtmpPublisher(PublishTarget target, std::uint32_t epoch_ms, const std::array<std::byte, rtmp_random_size>& random)
    : target_(std::move(target)), handshake_(epoch_ms, random) {}

std::vector<std::byte> RtmpPublisher::start() { return handshake_.start(); }

void RtmpPublisher::command(std::uint8_t chunk_stream, std::uint32_t stream_id, const std::vector<Amf0Value>& values, std::vector<std::byte>& out) {
    writer_.write(chunk_stream, {rtmp_type::command_amf0, 0, stream_id, amf0_encode(values)}, out);
}

std::vector<std::byte> RtmpPublisher::receive(std::span<const std::byte> data) {
    std::vector<std::byte> out;
    if (phase_ == PublishPhase::failed) return out;
    try {
        if (!handshake_.done()) {
            const auto used = handshake_.feed(data, out);
            data = data.subspan(used);
            if (!handshake_.done()) return out;
            // Handshake complete: the session opens with our chunk size and the connect command.
            writer_.write(control_stream, set_chunk_size_message(chunk_size), out);
            writer_.chunk_size(chunk_size);
            command(command_stream, 0, {Amf0Value::of("connect"), Amf0Value::of(connect_transaction), Amf0Value::object({
                {"app", Amf0Value::of(target_.app)}, {"type", Amf0Value::of("nonprivate")},
                {"flashVer", Amf0Value::of("FMLE/3.0 (compatible; odeum-program)")}, {"tcUrl", Amf0Value::of(target_.tc_url)}})}, out);
            phase_ = PublishPhase::connecting;
        }
        if (data.empty()) return out;
        if (auto sequence = ack_.received(data.size())) writer_.write(control_stream, acknowledgement_message(*sequence), out);
        for (const auto& message : reader_.feed(data)) handle(message, out);
    } catch (...) {
        phase_ = PublishPhase::failed;
        throw;
    }
    return out;
}

void RtmpPublisher::after_connect(std::vector<std::byte>& out) {
    const auto key = Amf0Value::of(target_.stream_key);
    command(command_stream, 0, {Amf0Value::of("releaseStream"), Amf0Value::of(release_transaction), Amf0Value::null(), key}, out);
    command(command_stream, 0, {Amf0Value::of("FCPublish"), Amf0Value::of(fcpublish_transaction), Amf0Value::null(), key}, out);
    command(command_stream, 0, {Amf0Value::of("createStream"), Amf0Value::of(create_transaction), Amf0Value::null()}, out);
    phase_ = PublishPhase::creating_stream;
}

void RtmpPublisher::handle(const RtmpMessage& message, std::vector<std::byte>& out) {
    switch (message.type) {
    case rtmp_type::window_ack_size:
        if (message.payload.size() >= 4) ack_.window(get_u32(message.payload, 0));
        return;
    case rtmp_type::set_peer_bandwidth:
        writer_.write(control_stream, window_ack_size_message(client_window), out);
        return;
    case rtmp_type::user_control:
        // Ping request (event 6) -> ping response (event 7).
        if (message.payload.size() >= 6 && get_u16(message.payload, 0) == 6)
            writer_.write(control_stream, ping_response_message(get_u32(message.payload, 2)), out);
        return;
    case rtmp_type::command_amf0: break;
    default: return;
    }
    const auto values = amf0_decode(message.payload);
    if (values.empty() || values[0].type != Amf0Value::Type::string) return;
    const auto& name = values[0].string;
    const double transaction = values.size() > 1 && values[1].type == Amf0Value::Type::number ? values[1].number : 0;
    if (name == "_error") {
        if (transaction == connect_transaction) throw RtmpError("connect_rejected", "The server refused the connection: " + status_code(values));
        if (transaction == create_transaction || transaction == publish_transaction)
            throw RtmpError("publish_rejected", "The server refused the stream: " + status_code(values));
        return; // releaseStream / FCPublish errors are normal on a fresh key
    }
    if (name == "_result" && transaction == connect_transaction && phase_ == PublishPhase::connecting) {
        after_connect(out);
    } else if (name == "_result" && transaction == create_transaction && phase_ == PublishPhase::creating_stream) {
        if (values.size() < 4 || values[3].type != Amf0Value::Type::number) throw RtmpError("publish_rejected", "createStream returned no stream id");
        stream_id_ = static_cast<std::uint32_t>(values[3].number);
        command(stream_command_stream, stream_id_, {Amf0Value::of("publish"), Amf0Value::of(publish_transaction), Amf0Value::null(),
                                                    Amf0Value::of(target_.stream_key), Amf0Value::of("live")}, out);
        phase_ = PublishPhase::publishing_requested;
    } else if (name == "onStatus") {
        const auto code = status_code(values);
        if (code == "NetStream.Publish.Start") phase_ = PublishPhase::publishing;
        else if (code.find("Failed") != std::string::npos || code.find("BadName") != std::string::npos || code.find("Rejected") != std::string::npos)
            throw RtmpError("publish_rejected", "The server refused the stream: " + code);
    }
}

void RtmpPublisher::send(const FlvTag& tag, std::vector<std::byte>& out) const {
    if (phase_ != PublishPhase::publishing) throw RtmpError("not_publishing", "The stream is not published yet");
    switch (tag.type) {
    case FlvTagType::audio: writer_.write(audio_stream, {rtmp_type::audio, tag.timestamp_ms, stream_id_, tag.body}, out); break;
    case FlvTagType::video: writer_.write(video_stream, {rtmp_type::video, tag.timestamp_ms, stream_id_, tag.body}, out); break;
    case FlvTagType::script: writer_.write(data_stream, {rtmp_type::data_amf0, tag.timestamp_ms, stream_id_, tag.body}, out); break;
    }
}
}
