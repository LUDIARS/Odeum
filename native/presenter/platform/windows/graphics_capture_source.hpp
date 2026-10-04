#pragma once
#include "../../core/capture_source.hpp"
#include <memory>

namespace odeum::presenter::windows {
// Windows Graphics Capture (Windows 10 1903+). Displays are listed primary first, then visible
// top-level windows of other processes. Frames are D3D11 BGRA textures on the source's own
// device, delivered on the free-threaded frame pool's thread. Windows marked
// WDA_EXCLUDEFROMCAPTURE (the presenter's overlay and panel) never appear in them.
class GraphicsCaptureSource final : public CaptureSource {
public:
    GraphicsCaptureSource();
    ~GraphicsCaptureSource() override;
    std::vector<CaptureTarget> targets() override;
    void start(const CaptureTarget&, const StreamSettings&, FrameSink, ErrorSink) override;
    void stop() override;
    CapturePermission permission() override;
    void request_permission() override {}
private:
    struct Session;
    // Shared so a frame callback already running when stop() is called keeps its state alive.
    std::shared_ptr<Session> session_;
};
}
