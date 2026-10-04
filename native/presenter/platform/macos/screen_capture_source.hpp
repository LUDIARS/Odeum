#pragma once
#include "../../core/capture_source.hpp"
#include <memory>

namespace odeum::presenter::macos {
// ScreenCaptureKit video. Displays are listed main display first, then on-screen windows of other
// applications. A display capture uses an SCContentFilter that excludes this application, so
// the overlay and the panel never reach the stream. Frames are NV12 CVPixelBuffers at the stream
// size (aspect ratio kept), delivered on a private serial queue.
class ScreenCaptureSource final : public CaptureSource {
public:
    ScreenCaptureSource();
    ~ScreenCaptureSource() override;
    std::vector<CaptureTarget> targets() override;
    void start(const CaptureTarget&, const StreamSettings&, FrameSink, ErrorSink) override;
    void stop() override;
    CapturePermission permission() override;
    void request_permission() override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
