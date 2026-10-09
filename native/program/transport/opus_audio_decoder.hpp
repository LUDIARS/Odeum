#pragma once
#include <cstddef>
#include <span>
#include <vector>

struct OpusDecoder;

namespace odeum::program {
// libopus at 48 kHz stereo. One per input; decode() runs on that input's network thread.
class OpusAudioDecoder {
public:
    OpusAudioDecoder();
    ~OpusAudioDecoder();
    OpusAudioDecoder(const OpusAudioDecoder&) = delete;
    OpusAudioDecoder& operator=(const OpusAudioDecoder&) = delete;
    // Interleaved stereo float PCM of one packet (20 ms from the senders; other durations are
    // returned as decoded). Empty for a packet libopus rejects.
    std::vector<float> decode(std::span<const std::byte> packet);
private:
    OpusDecoder* decoder_ = nullptr;
};
}
