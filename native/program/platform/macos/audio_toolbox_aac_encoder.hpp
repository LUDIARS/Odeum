#pragma once
#include "../../core/media_codecs.hpp"
#include <memory>

namespace odeum::program::macos {
// AudioToolbox's AudioConverter: 48 kHz stereo float PCM to raw AAC-LC frames (1024 samples).
class AudioToolboxAacEncoder final : public AacEncoder {
public:
    AudioToolboxAacEncoder();
    ~AudioToolboxAacEncoder() override;
    void configure(int bitrate_kbps) override;
    std::vector<AacPacket> encode(std::span<const float> frame, std::int64_t timestamp_us) override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
