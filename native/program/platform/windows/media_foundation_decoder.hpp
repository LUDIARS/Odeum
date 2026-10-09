#pragma once
#include "../../core/media_codecs.hpp"
#include <memory>

namespace odeum::program::windows {
// Media Foundation's H.264 decoder MFT (synchronous, low-latency) to NV12 in memory. The MFT
// announces the picture size with a stream change; the visible area (minimum display aperture)
// is cropped out of the padded output.
class MediaFoundationDecoder final : public VideoDecoder {
public:
    MediaFoundationDecoder();
    ~MediaFoundationDecoder() override;
    std::vector<Nv12Picture> decode(std::span<const std::byte> access_unit, std::int64_t timestamp_us) override;
    void reset() override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
