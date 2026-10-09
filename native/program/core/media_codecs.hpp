#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace odeum::program {
// A decoded picture: tightly packed NV12 (Y plane, then interleaved UV), even width and height.
struct Nv12Picture {
    int width{}, height{};
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
    std::int64_t timestamp_us{};
};

// H.264 Annex-B access units in, NV12 pictures out (VideoToolbox / Media Foundation). One per
// input slot; decode() runs on that slot's network thread.
class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;
    // Pictures that became ready (none while the decoder waits for an IDR or buffers).
    // Throws std::runtime_error for a stream the decoder cannot handle; the caller resets.
    virtual std::vector<Nv12Picture> decode(std::span<const std::byte> access_unit, std::int64_t timestamp_us) = 0;
    // Forgets the stream (the slot's sender changed); the next picture needs a new IDR.
    virtual void reset() = 0;
};

struct AacPacket {
    std::vector<std::byte> data; // raw AAC-LC frame, no ADTS header
    std::int64_t timestamp_us{};
};

// AAC-LC at 48 kHz stereo (AudioToolbox / Media Foundation). Takes 20 ms float frames and
// returns whatever 1024-sample AAC frames completed.
class AacEncoder {
public:
    virtual ~AacEncoder() = default;
    virtual void configure(int bitrate_kbps) = 0;
    virtual std::vector<AacPacket> encode(std::span<const float> frame, std::int64_t timestamp_us) = 0;
};

// Where the YouTube stream key is kept: the macOS Keychain or a DPAPI-protected file on
// Windows. Never a plain file, never logged.
class SecretStore {
public:
    virtual ~SecretStore() = default;
    virtual std::optional<std::string> load() = 0;
    virtual void save(const std::string& key) = 0;
    virtual void erase() = 0;
};
}
