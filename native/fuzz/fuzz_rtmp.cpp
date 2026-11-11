// libFuzzer entry: bytes from the RTMP ingest server after the handshake (program/core
// rtmp_chunk.cpp and amf0.cpp, as RtmpPublisher reads them).
#include "core/amf0.hpp"
#include "core/rtmp_chunk.hpp"
#include "core/rtmp_handshake.hpp"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
    try {
        odeum::program::ChunkReader reader;
        // Split once so partial chunks are exercised too.
        const auto half = bytes.size() / 2;
        auto messages = reader.feed(bytes.first(half));
        for (auto& message : reader.feed(bytes.subspan(half))) messages.push_back(std::move(message));
        for (const auto& message : messages) {
            if (message.type == odeum::program::rtmp_type::command_amf0) (void)odeum::program::amf0_decode(message.payload);
        }
    } catch (const odeum::program::RtmpError&) {
    } catch (const odeum::program::Amf0Error&) {
    }
    return 0;
}
