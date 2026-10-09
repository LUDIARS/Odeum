#pragma once
#include "../../core/media_codecs.hpp"
#include <memory>

namespace odeum::program::macos {
// VideoToolbox H.264 decoding to NV12 (video range) in memory. The session is (re)created from
// the SPS/PPS of each IDR whenever they change; frames before the first IDR are skipped.
class VideoToolboxDecoder final : public VideoDecoder {
public:
    VideoToolboxDecoder();
    ~VideoToolboxDecoder() override;
    std::vector<Nv12Picture> decode(std::span<const std::byte> access_unit, std::int64_t timestamp_us) override;
    void reset() override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
