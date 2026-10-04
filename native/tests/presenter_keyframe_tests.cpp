#include "check.hpp"
#include "core/keyframe_request.hpp"
#include "core/video_pipeline.hpp"
#include <vector>
using namespace odeum::presenter;

namespace {
// Records what the pipeline asked for instead of encoding.
class FakeEncoder final : public VideoEncoder {
public:
    std::vector<bool> forced;
    int configured = 0, stopped = 0;
    Sink sink;
    void configure(const StreamSettings&, Sink s) override { ++configured; sink = std::move(s); }
    void encode(const VideoFrame& frame, bool force) override {
        forced.push_back(force);
        sink({{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}, static_cast<std::byte>(force ? 0x65 : 0x41)}, frame.timestamp_us, force});
    }
    void stop() override { ++stopped; }
};

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> result;
    for (int v : values) result.push_back(static_cast<std::byte>(v));
    return result;
}
// RTCP PSFB (PT 206): FMT 1 = PLI (12 bytes), FMT 4 = FIR (20 bytes).
const auto pli = bytes({0x81, 206, 0, 2, 0, 0, 0, 1, 0x12, 0x34, 0x56, 0x78});
const auto fir = bytes({0x84, 206, 0, 4, 0, 0, 0, 1, 0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78, 1, 0, 0, 0});
const auto receiver_report = bytes({0x80, 201, 0, 1, 0, 0, 0, 1});
const auto rtp = bytes({0x80, 102, 0, 1, 0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78, 0x65});
const auto remb = bytes({0x8f, 206, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0}); // PSFB FMT 15, not a keyframe request
}

int main() { return run([] {
    check(is_keyframe_request(pli), "PLI");
    check(is_keyframe_request(fir), "FIR");
    auto compound = receiver_report;
    compound.insert(compound.end(), pli.begin(), pli.end());
    check(is_keyframe_request(compound), "Compound RR + PLI");
    check(!is_keyframe_request(receiver_report), "Receiver report alone");
    check(!is_keyframe_request(rtp), "RTP media");
    check(!is_keyframe_request(remb), "Other PSFB");
    check(!is_keyframe_request(bytes({0x81, 206, 0, 9, 0, 0, 0, 1})), "Length past the end");
    check(!is_keyframe_request(bytes({0x81, 206, 0, 1, 0, 0, 0, 1})), "PLI too short");
    check(!is_keyframe_request({}), "Empty");

    FakeEncoder encoder;
    VideoPipeline pipeline(encoder);
    std::vector<EncodedFrame> sent;
    StreamSettings settings;
    settings.fps = 30;
    pipeline.start(settings, [&](const EncodedFrame& frame) { sent.push_back(frame); });
    check(encoder.configured == 1 && pipeline.running(), "Configured");
    const std::int64_t interval = 1000000 / 30;
    std::int64_t t = 0;
    const auto frame = [&] { pipeline.on_frame({1920, 1080, t, nullptr}); t += interval; };
    frame();
    frame();
    check(encoder.forced == std::vector<bool>({true, false}), "First frame is an IDR");

    // A keyframe request from the relay (network thread) forces exactly the next frame.
    check(pipeline.on_rtcp(pli), "PLI accepted");
    check(!pipeline.on_rtcp(receiver_report), "RR ignored");
    frame();
    frame();
    check(encoder.forced == std::vector<bool>({true, false, true, false}), "PLI forces the next frame");
    check(pipeline.on_rtcp(fir), "FIR accepted");
    pipeline.on_rtcp(pli); // two requests before one frame still give one IDR
    frame();
    frame();
    check(encoder.forced == std::vector<bool>({true, false, true, false, true, false}), "FIR and coalescing");
    check(pipeline.keyframes_forced() == 3 && sent.size() == 6 && sent[2].keyframe && !sent[3].keyframe, "Encoded output reaches the sender");

    // Frames faster than the configured rate are dropped before the encoder.
    pipeline.on_frame({1920, 1080, t - interval + 1000, nullptr});
    check(encoder.forced.size() == 6, "Frame-rate gate");
    frame();
    check(encoder.forced.size() == 7, "On-time frame passes");

    pipeline.stop();
    check(encoder.stopped == 1 && !pipeline.running(), "Stopped");
    pipeline.on_frame({1920, 1080, t + interval * 10, nullptr});
    check(encoder.forced.size() == 7, "No frames after stop");
    pipeline.start(settings, [&](const EncodedFrame& frame) { sent.push_back(frame); });
    pipeline.on_frame({1920, 1080, 0, nullptr});
    check(encoder.forced.back(), "Restart begins with an IDR");
}); }
