#pragma once
#include "../../core/media_codecs.hpp"
#include <memory>

namespace odeum::program::windows {
// Media Foundation's AAC encoder MFT: 16-bit PCM 48 kHz stereo in, raw AAC-LC frames out. The
// MFT only offers 96/128/160/192 kbps; the nearest one is used.
class MediaFoundationAacEncoder final : public AacEncoder {
public:
    MediaFoundationAacEncoder();
    ~MediaFoundationAacEncoder() override;
    void configure(int bitrate_kbps) override;
    std::vector<AacPacket> encode(std::span<const float> frame, std::int64_t timestamp_us) override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
