// Boundary tests for odeum-program's inputs: AMF0 payloads and the RTMP chunk stream from the
// ingest server, program.json's text and a recording's archive.json.
#include "check.hpp"
#include "core/amf0.hpp"
#include "core/archive_reader.hpp"
#include "core/input_frame_limits.hpp"
#include "core/program_settings.hpp"
#include "core/rtmp_chunk.hpp"
#include "core/rtmp_handshake.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
using namespace odeum;
using namespace odeum::program;

namespace {
template<class E, class F> bool throws(F action) {
    try { action(); } catch (const E&) { return true; }
    return false;
}
std::vector<std::byte> nested_objects(std::size_t depth) {
    std::vector<std::byte> out;
    for (std::size_t i = 0; i < depth; ++i) for (int b : {3, 0, 1, int{'k'}}) out.push_back(static_cast<std::byte>(b));
    out.push_back(std::byte{5});
    for (std::size_t i = 0; i < depth; ++i) for (int b : {0, 0, 9}) out.push_back(static_cast<std::byte>(b));
    return out;
}
// A type-0 chunk header for chunk stream `id` (one- or two-byte basic header).
void header(std::vector<std::byte>& out, std::uint32_t id, std::uint32_t length, std::uint8_t type) {
    if (id < 64) out.push_back(static_cast<std::byte>(id));
    else { out.push_back(std::byte{0}); out.push_back(static_cast<std::byte>(id - 64)); }
    for (int i = 0; i < 3; ++i) out.push_back(std::byte{0});
    for (int shift : {16, 8, 0}) out.push_back(static_cast<std::byte>((length >> shift) & 0xff));
    out.push_back(static_cast<std::byte>(type));
    for (int i = 0; i < 4; ++i) out.push_back(std::byte{0});
}
std::vector<std::byte> one_byte_message(std::uint32_t id) {
    std::vector<std::byte> out;
    header(out, id, 1, rtmp_type::audio);
    out.push_back(std::byte{0xAF});
    return out;
}

void amf0_limits() {
    check(amf0_decode(std::vector<std::byte>(max_amf0_values, std::byte{5})).size() == max_amf0_values, "AMF0 values at the limit");
    check(throws<Amf0Error>([] { amf0_decode(std::vector<std::byte>(max_amf0_values + 1, std::byte{5})); }), "One AMF0 value over");
    // Property values count too: an object with 4095 null properties is 4096 values.
    std::vector<std::byte> object{std::byte{3}};
    for (std::size_t i = 0; i + 1 < max_amf0_values; ++i) for (int b : {0, 1, int{'p'}, 5}) object.push_back(static_cast<std::byte>(b));
    for (int b : {0, 0, 9}) object.push_back(static_cast<std::byte>(b));
    check(amf0_decode(object).at(0).properties.size() == max_amf0_values - 1, "Object at the value limit");
    object.insert(object.begin() + 1, {std::byte{0}, std::byte{1}, std::byte{'q'}, std::byte{5}});
    check(throws<Amf0Error>([&] { amf0_decode(object); }), "Object one value over");
    check(amf0_decode(nested_objects(16)).size() == 1, "AMF0 nesting at the limit");
    check(throws<Amf0Error>([] { amf0_decode(nested_objects(17)); }), "AMF0 nesting one level over");
    check(throws<Amf0Error>([] { amf0_decode(std::vector<std::byte>{std::byte{12}, std::byte{0x7f}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}}); }),
          "Long string longer than the payload");
}

void chunk_limits() {
    ChunkReader reader;
    for (std::uint32_t id = 2; id < 2 + max_rtmp_chunk_streams; ++id) check(reader.feed(one_byte_message(id)).size() == 1, "Chunk stream");
    check(reader.feed(one_byte_message(2)).size() == 1, "Known chunk streams keep working at the limit");
    check(throws<RtmpError>([&] { reader.feed(one_byte_message(2 + max_rtmp_chunk_streams)); }), "One chunk stream over the limit");

    // Partly received messages: 8 MiB chunks of 16 MiB - 1 messages on four streams hold 32 MiB.
    ChunkReader partial;
    // The 24-bit length field caps a message at 0xFFFFFF bytes (16 MiB - 1).
    constexpr std::uint32_t chunk = 8u << 20, message = 0xFFFFFF;
    std::vector<std::byte> size;
    header(size, 2, 4, rtmp_type::set_chunk_size);
    for (int shift : {24, 16, 8, 0}) size.push_back(static_cast<std::byte>((chunk >> shift) & 0xff));
    check(partial.feed(size).size() == 1 && partial.chunk_size() == chunk, "Set Chunk Size");
    for (std::uint32_t id = 3; id < 7; ++id) {
        std::vector<std::byte> data;
        header(data, id, message, rtmp_type::video);
        data.resize(data.size() + chunk, std::byte{1});
        check(partial.feed(data).empty(), "Partial message held");
    }
    std::vector<std::byte> more;
    header(more, 7, message, rtmp_type::video);
    check(throws<RtmpError>([&] { partial.feed(more); }), "Pending bytes over the limit (header only)");

    // Unparsed input counts too: one incomplete chunk of the largest message.
    ChunkReader unparsed;
    std::vector<std::byte> big;
    header(big, 2, 4, rtmp_type::set_chunk_size);
    for (int shift : {24, 16, 8, 0}) big.push_back(static_cast<std::byte>((message >> shift) & 0xff));
    header(big, 3, message, rtmp_type::video);
    big.resize(big.size() + message - 1, std::byte{1});
    check(unparsed.feed(big).size() == 1, "Incomplete largest chunk is held");
    check(unparsed.feed(std::vector<std::byte>(1, std::byte{1})).size() == 1, "Then completed");
}

void settings_limits() {
    check(settings_from_text(R"({"volume":0.5})").volume == 0.5, "Settings text");
    const std::string padded = R"({"font":")" + std::string(max_settings_file_bytes - 11, 'f') + "\"}";
    check(padded.size() == max_settings_file_bytes && settings_from_text(padded).font.size() == max_settings_file_bytes - 11,
          "Settings at the size limit");
    check(throws<std::invalid_argument>([&] { settings_from_text(padded + " "); }), "Settings one byte over");
    settings_from_text(R"({"x":)" + std::string(15, '[') + std::string(15, ']') + "}");
    check(throws<std::invalid_argument>([] { settings_from_text(R"({"x":)" + std::string(16, '[') + std::string(16, ']') + "}"); }),
          "Settings nested past the limit");
    check(throws<std::invalid_argument>([] { settings_from_text("{\"video\":"); }), "Broken settings");
}

void archive_limits() {
    const auto root = std::filesystem::temp_directory_path() /
        ("odeum-archive-limits-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto directory = root / "project" / "20261111-000000";
    std::filesystem::create_directories(directory);
    const unsigned char flv[13] = {'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0};
    std::ofstream(directory / "program.flv", std::ios::binary).write(reinterpret_cast<const char*>(flv), sizeof(flv));
    const auto manifest = [&](const std::string& text) { std::ofstream(directory / "archive.json", std::ios::binary | std::ios::trunc) << text; };
    const std::string base = R"({"version":1,"status":"ready","file":"program.flv","bytes":13,"pad":")";
    const auto padded = [&](std::size_t size) { return base + std::string(size - base.size() - 2, ' ') + "\"}"; };
    try {
        manifest(padded(16384));
        check(project_archives(root, "project").size() == 1, "Manifest at the size limit");
        check(!ArchiveReader(directory / "program.flv").next(), "Header-only recording has no tags");
        manifest(padded(16385));
        check(project_archives(root, "project").empty(), "Manifest one byte over is not finalized");
        manifest(R"({"version":1,"status":"ready","file":"program.flv","bytes":13,"x":[1]})");
        check(project_archives(root, "project").size() == 1, "Manifest at the depth limit");
        manifest(R"({"version":1,"status":"ready","file":"program.flv","bytes":13,"x":[[1]]})");
        check(throws<ProtocolError>([&] { project_archives(root, "project"); }), "Manifest nested past the limit");
        std::string wide = R"({"version":1,"status":"ready","file":"program.flv","bytes":13,"x":[)";
        for (int i = 0; i < 64; ++i) wide += i ? ",0" : "0";
        manifest(wide + "]}");
        check(throws<ProtocolError>([&] { project_archives(root, "project"); }), "Manifest with too many values");
        manifest("{\"version\":");
        check(throws<Json::parse_error>([&] { project_archives(root, "project"); }), "Broken manifest is a catalog error");
        std::filesystem::remove_all(root);
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
}

void frame_limits() {
    check(accepts_input_frame(true, max_input_video_frame_bytes) && !accepts_input_frame(true, max_input_video_frame_bytes + 1), "Video frame limit");
    check(accepts_input_frame(false, max_input_audio_frame_bytes) && !accepts_input_frame(false, max_input_audio_frame_bytes + 1), "Audio frame limit");
    check(!accepts_input_frame(true, 0) && !accepts_input_frame(false, 0), "Empty frames are dropped");
}
}

int main() { return run([] { amf0_limits(); chunk_limits(); settings_limits(); archive_limits(); frame_limits(); }); }
