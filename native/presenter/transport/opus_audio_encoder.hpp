#pragma once
#include "../core/audio_source.hpp"

struct OpusEncoder;

namespace odeum::presenter {
// libopus at 48 kHz stereo, 20 ms frames, tuned for a talk with occasional music.
class OpusAudioEncoder final : public AudioEncoder {
public:
    OpusAudioEncoder();
    ~OpusAudioEncoder() override;
    OpusAudioEncoder(const OpusAudioEncoder&) = delete;
    OpusAudioEncoder& operator=(const OpusAudioEncoder&) = delete;
    void configure(int bitrate_kbps) override;
    std::vector<std::byte> encode(std::span<const float> frame) override;
private:
    OpusEncoder* encoder_ = nullptr;
};
}
