#include "check.hpp"
#include "core/bytes.hpp"
#include "core/rtmp_publisher.hpp"
#include <algorithm>
#include <initializer_list>
using namespace odeum::program;

namespace {
std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (int v : values) out.push_back(static_cast<std::byte>(v));
    return out;
}

template<class Function> std::string rtmp_error(Function action) {
    try { action(); } catch (const RtmpError& error) { return error.code; }
    throw std::runtime_error("Expected an RtmpError");
}

std::array<std::byte, rtmp_random_size> random_bytes() {
    std::array<std::byte, rtmp_random_size> random{};
    for (std::size_t i = 0; i < random.size(); ++i) random[i] = static_cast<std::byte>(i * 7);
    return random;
}

void amf0() {
    check(amf0_encode({Amf0Value::of(1.0)}) == bytes({0, 0x3F, 0xF0, 0, 0, 0, 0, 0, 0}), "Number");
    check(amf0_encode({Amf0Value::of("live")}) == bytes({2, 0, 4, 'l', 'i', 'v', 'e'}), "String");
    check(amf0_encode({Amf0Value::of(true), Amf0Value::null()}) == bytes({1, 1, 5}), "Boolean and null");
    check(amf0_encode({Amf0Value::object({{"app", Amf0Value::of("live2")}})}) ==
          bytes({3, 0, 3, 'a', 'p', 'p', 2, 0, 5, 'l', 'i', 'v', 'e', '2', 0, 0, 9}), "Object");
    check(amf0_encode({Amf0Value::ecma_array({{"w", Amf0Value::of(2.0)}})}) ==
          bytes({8, 0, 0, 0, 1, 0, 1, 'w', 0, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9}), "ECMA array");
    const std::vector<Amf0Value> values{Amf0Value::of("_result"), Amf0Value::of(1.0), Amf0Value::object({
        {"fmsVer", Amf0Value::of("FMS/3,0,1,123")}, {"nested", Amf0Value::object({{"ok", Amf0Value::of(false)}})}}), Amf0Value::null()};
    const auto decoded = amf0_decode(amf0_encode(values));
    check(decoded == values, "Round trip");
    check(decoded[2].find("nested")->find("ok")->boolean == false && decoded[2].find("missing") == nullptr, "Property lookup");
    bool refused = false;
    try { amf0_decode(bytes({2, 0, 9, 'x'})); } catch (const Amf0Error&) { refused = true; }
    check(refused, "Truncated string refused");
    refused = false;
    try { amf0_decode(bytes({0x11})); } catch (const Amf0Error&) { refused = true; }
    check(refused, "Unknown marker refused");
}

void handshake() {
    RtmpHandshake client(0x01020304, random_bytes());
    const auto c0c1 = client.start();
    check(c0c1.size() == 1 + rtmp_handshake_size && c0c1[0] == std::byte{3}, "C0 is version 3");
    check(std::vector<std::byte>(c0c1.begin() + 1, c0c1.begin() + 9) == bytes({1, 2, 3, 4, 0, 0, 0, 0}), "C1 time and zero");
    check(c0c1[9] == std::byte{0} && c0c1[10] == std::byte{7}, "C1 random");

    std::vector<std::byte> server(1 + 2 * rtmp_handshake_size + 3);
    server[0] = std::byte{3};
    for (std::size_t i = 1; i <= rtmp_handshake_size; ++i) server[i] = static_cast<std::byte>(i % 251);
    std::vector<std::byte> out;
    std::size_t used = client.feed(std::span(server).subspan(0, 700), out);
    check(used == 700 && out.empty() && !client.done(), "Partial S1 sends nothing");
    used += client.feed(std::span(server).subspan(700), out);
    check(out.size() == rtmp_handshake_size && std::equal(out.begin(), out.end(), server.begin() + 1), "C2 echoes S1");
    check(client.done() && used == 1 + 2 * rtmp_handshake_size, "S2 completes the handshake; later bytes are left for chunks");

    RtmpHandshake wrong(0, random_bytes());
    check(rtmp_error([&] { std::vector<std::byte> o; wrong.feed(bytes({6}), o); }) == "unsupported_version", "S0 version checked");
}

void chunks() {
    ChunkWriter writer;
    RtmpMessage big{rtmp_type::command_amf0, 0, 0, std::vector<std::byte>(300, std::byte{0x5A})};
    std::vector<std::byte> out;
    writer.write(3, big, out);
    check(out.size() == 12 + 300 + 2, "300 bytes at chunk size 128 take three chunks");
    check(std::vector<std::byte>(out.begin(), out.begin() + 12) == bytes({0x03, 0, 0, 0, 0, 1, 0x2C, 20, 0, 0, 0, 0}), "Type 0 header");
    check(out[12 + 128] == std::byte{0xC3} && out[12 + 128 + 1 + 128] == std::byte{0xC3}, "Type 3 continuation headers");

    ChunkReader reader;
    auto messages = reader.feed(out);
    check(messages.size() == 1 && messages[0] == big, "Reader reassembles chunks");

    out.clear();
    RtmpMessage late{rtmp_type::video, 0x01000000, 1, std::vector<std::byte>(130, std::byte{1})};
    writer.write(6, late, out);
    check(std::vector<std::byte>(out.begin(), out.begin() + 16) == bytes({0x06, 0xFF, 0xFF, 0xFF, 0, 0, 130, 9, 1, 0, 0, 0, 1, 0, 0, 0}),
          "Extended timestamp after the type 0 header");
    check(std::vector<std::byte>(out.begin() + 16 + 128, out.begin() + 16 + 128 + 5) == bytes({0xC6, 1, 0, 0, 0}), "Type 3 repeats the extended timestamp");
    // Fed one byte at a time to exercise partial headers.
    std::vector<RtmpMessage> collected;
    for (auto b : out) for (auto& m : reader.feed(std::span(&b, 1))) collected.push_back(std::move(m));
    check(collected.size() == 1 && collected[0] == late, "Extended timestamp read back");

    // Type 0, then type 2 (same length/type, delta) and type 3 (repeat the delta) messages.
    ChunkReader compact;
    messages = compact.feed(bytes({0x04, 0, 0, 100, 0, 0, 2, 8, 1, 0, 0, 0, 0xAA, 0xBB, 0x84, 0, 0, 20, 0xCC, 0xDD, 0xC4, 0xEE, 0xFF}));
    check(messages.size() == 3 && messages[0].timestamp == 100 && messages[1].timestamp == 120 && messages[2].timestamp == 140, "Deltas");
    check(messages[2].payload == bytes({0xEE, 0xFF}) && messages[2].stream_id == 1 && messages[2].type == 8, "Type 3 inherits the header");
    check(rtmp_error([] { ChunkReader r; r.feed(bytes({0x45, 0, 0, 0, 0, 0, 0, 0})); }) == "invalid_chunk", "Unknown chunk stream");

    // Set Chunk Size from the peer applies to the following chunks.
    ChunkWriter wide;
    out.clear();
    writer.write(2, set_chunk_size_message(4096), out);
    wide.chunk_size(4096);
    RtmpMessage large{rtmp_type::audio, 5, 1, std::vector<std::byte>(4000, std::byte{2})};
    wide.write(4, large, out);
    check(out.size() == 16 + 12 + 4000, "Chunk size 4096 sends 4000 bytes in one chunk");
    ChunkReader follows;
    messages = follows.feed(out);
    check(messages.size() == 2 && follows.chunk_size() == 4096 && messages[1] == large, "Reader follows Set Chunk Size");

    AckWindow none;
    check(!none.received(5000), "No window, no acknowledgement");
    AckWindow ack;
    ack.window(1000);
    check(!ack.received(600) && ack.received(500) == 1100u, "Acknowledged once the window is reached");
    check(!ack.received(999) && ack.received(1) == 2100u, "Counted from the last acknowledgement");
    check(acknowledgement_message(2100).payload == bytes({0, 0, 0x08, 0x34}), "Acknowledgement payload");
    check(ping_response_message(9).payload == bytes({0, 7, 0, 0, 0, 9}), "Ping response");
}

std::vector<std::byte> server_command(const std::vector<Amf0Value>& values, std::uint32_t stream_id = 0) {
    std::vector<std::byte> out;
    ChunkWriter().write(3, {rtmp_type::command_amf0, 0, stream_id, amf0_encode(values)}, out);
    return out;
}

void publish() {
    const std::string key = "abcd-1234-efgh";
    RtmpPublisher publisher({"rtmps://a.rtmps.youtube.com:443/live2", "live2", key}, 0, random_bytes());
    check(publisher.start().size() == 1 + rtmp_handshake_size, "C0 + C1");
    std::vector<std::byte> server(1 + 2 * rtmp_handshake_size);
    server[0] = std::byte{3};
    auto out = publisher.receive(server);
    check(publisher.phase() == PublishPhase::connecting, "Connect after the handshake");
    ChunkReader seen;
    auto sent = seen.feed(std::span(out).subspan(rtmp_handshake_size));
    check(sent.size() == 2 && sent[0].type == rtmp_type::set_chunk_size && get_u32(sent[0].payload, 0) == 4096, "Set Chunk Size 4096 first");
    auto connect = amf0_decode(sent[1].payload);
    check(connect[0].string == "connect" && connect[1].number == 1 && connect[2].find("app")->string == "live2" &&
          connect[2].find("tcUrl")->string == "rtmps://a.rtmps.youtube.com:443/live2", "connect(app, tcUrl)");

    std::vector<std::byte> control;
    ChunkWriter().write(2, window_ack_size_message(5000000), control);
    RtmpMessage bandwidth{rtmp_type::set_peer_bandwidth, 0, 0, bytes({0, 0x4C, 0x4B, 0x40, 2})};
    ChunkWriter().write(2, bandwidth, control);
    auto result = server_command({Amf0Value::of("_result"), Amf0Value::of(1.0), Amf0Value::null(),
                                  Amf0Value::object({{"code", Amf0Value::of("NetConnection.Connect.Success")}})});
    control.insert(control.end(), result.begin(), result.end());
    sent = seen.feed(publisher.receive(control));
    check(sent.size() == 4 && sent[0].type == rtmp_type::window_ack_size, "Set Peer Bandwidth answered with Window Ack Size");
    check(amf0_decode(sent[1].payload)[0].string == "releaseStream" && amf0_decode(sent[1].payload)[3].string == key, "releaseStream(key)");
    check(amf0_decode(sent[2].payload)[0].string == "FCPublish" && amf0_decode(sent[3].payload)[0].string == "createStream", "FCPublish, createStream");
    check(publisher.phase() == PublishPhase::creating_stream, "Waiting for the stream id");

    sent = seen.feed(publisher.receive(server_command({Amf0Value::of("_result"), Amf0Value::of(4.0), Amf0Value::null(), Amf0Value::of(1.0)})));
    const auto publish_command = amf0_decode(sent.at(0).payload);
    check(sent[0].stream_id == 1 && publish_command[0].string == "publish" && publish_command[3].string == key &&
          publish_command[4].string == "live", "publish(key, live) on the new stream");
    check(rtmp_error([&] { std::vector<std::byte> o; publisher.send({FlvTagType::video, 0, bytes({1})}, o); }) == "not_publishing", "No media before Publish.Start");

    publisher.receive(server_command({Amf0Value::of("onStatus"), Amf0Value::of(0.0), Amf0Value::null(),
                                      Amf0Value::object({{"level", Amf0Value::of("status")}, {"code", Amf0Value::of("NetStream.Publish.Start")}})}, 1));
    check(publisher.publishing(), "Publishing after NetStream.Publish.Start");
    std::vector<std::byte> media;
    publisher.send({FlvTagType::video, 40, bytes({0x17, 1, 0, 0, 0})}, media);
    sent = seen.feed(media);
    check(sent.size() == 1 && sent[0].type == rtmp_type::video && sent[0].timestamp == 40 && sent[0].stream_id == 1, "Video message on the stream");

    RtmpPublisher rejected({"rtmps://h:443/live2", "live2", key}, 0, random_bytes());
    rejected.receive(server);
    rejected.receive(server_command({Amf0Value::of("_result"), Amf0Value::of(1.0), Amf0Value::null(), Amf0Value::null()}));
    rejected.receive(server_command({Amf0Value::of("_result"), Amf0Value::of(4.0), Amf0Value::null(), Amf0Value::of(1.0)}));
    try {
        rejected.receive(server_command({Amf0Value::of("onStatus"), Amf0Value::of(0.0), Amf0Value::null(), Amf0Value::object({
            {"code", Amf0Value::of("NetStream.Publish.BadName")}, {"description", Amf0Value::of(key + " is not valid")}})}, 1));
        throw std::runtime_error("Expected a rejection");
    } catch (const RtmpError& error) {
        check(error.code == "publish_rejected" && std::string(error.what()).find(key) == std::string::npos, "Rejection never repeats the key");
    }
    check(rejected.phase() == PublishPhase::failed, "A rejected publish is failed");
}
}

int main() { return run([] { amf0(); handshake(); chunks(); publish(); }); }
