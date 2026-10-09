#pragma once
#include "../../core/video_encoder.hpp"
#include <memory>

namespace odeum::presenter::windows {
// Media Foundation's H.264 encoder MFT in synchronous mode. By default Constrained Baseline, no
// B-frames, low-latency, peak-constrained VBR capped at the configured bitrate; H264Options
// switches to Main/High with CBR and B-frames (odeum-program). Captured D3D11 textures are read
// back and scaled to NV12 on the CPU (letterboxed to the stream size); a frame that already
// carries NV12 at the stream size is encoded as is.
// CODECAPI_AVEncVideoForceKeyFrame turns a forced frame into an IDR.
class MediaFoundationEncoder final : public VideoEncoder {
public:
    MediaFoundationEncoder();
    ~MediaFoundationEncoder() override;
    void configure(const StreamSettings&, Sink) override;
    void encode(const VideoFrame&, bool force_keyframe) override;
    void stop() override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
