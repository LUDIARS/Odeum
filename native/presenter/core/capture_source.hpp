#pragma once
#include "stream_settings.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace odeum::presenter {
enum class CaptureKind { display, window };

struct CaptureTarget {
    std::string id; // stable for the lifetime of the display/window, opaque outside the adapter
    CaptureKind kind{};
    std::string name;
    int width{}, height{};
    bool operator==(const CaptureTarget&) const = default;
};

// One captured picture. `native` is the platform's frame object (a retained CVPixelBuffer on
// macOS, a D3D11 texture on Windows); only the encoder of the same platform reads it, so Core
// passes it along without touching OS types. A frame composed in memory (odeum-program) carries
// `nv12` instead: tightly packed NV12 (BT.709 limited range) at width x height, which the
// encoders take as is.
struct VideoFrame {
    int width{}, height{};
    std::int64_t timestamp_us{};
    std::shared_ptr<void> native;
    std::shared_ptr<const std::vector<std::uint8_t>> nv12;
};

enum class CapturePermission { granted, denied, undetermined };

// Lists displays and windows and delivers frames of the chosen one. Frames arrive on the
// adapter's own thread; the adapter keeps the presenter's own windows out of every frame.
class CaptureSource {
public:
    using FrameSink = std::function<void(const VideoFrame&)>;
    using ErrorSink = std::function<void(const std::string&)>;
    virtual ~CaptureSource() = default;
    virtual std::vector<CaptureTarget> targets() = 0;
    // Replaces any running capture. Frames are scaled to fit settings.width x settings.height.
    virtual void start(const CaptureTarget&, const StreamSettings&, FrameSink, ErrorSink) = 0;
    // Once stop() returns, no sink is called any more.
    virtual void stop() = 0;
    virtual CapturePermission permission() = 0;
    // Asks the OS for screen recording permission; on macOS this opens System Settings.
    virtual void request_permission() = 0;
};
}
