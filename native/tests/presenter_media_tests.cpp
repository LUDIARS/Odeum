#include "check.hpp"
#include "core/event_queue.hpp"
#include "core/h264_bitstream.hpp"
#include "core/nv12_scaler.hpp"
#include "core/pcm_framer.hpp"
#include "core/presenter_settings.hpp"
#include <cmath>
using namespace odeum;
using namespace odeum::presenter;

namespace {
std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> result;
    for (int v : values) result.push_back(static_cast<std::byte>(v));
    return result;
}
}

int main() { return run([] {
    // AVCC (VideoToolbox) to Annex-B, and parameter sets in front of IDRs that lack them.
    const auto avcc = bytes({0, 0, 0, 2, 0x65, 0xaa, 0, 0, 0, 1, 0x06});
    const auto annexb = avcc_to_annexb(avcc, 4);
    check(annexb == bytes({0, 0, 0, 1, 0x65, 0xaa, 0, 0, 0, 1, 0x06}), "AVCC to Annex-B");
    check(contains_nal(annexb, nal_idr) && !contains_nal(annexb, nal_sps), "NAL types");
    check(contains_nal(bytes({0, 0, 1, 0x67, 1}), nal_sps), "Short start code");
    bool refused = false;
    try { avcc_to_annexb(bytes({0, 0, 0, 9, 0x65}), 4); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, "Truncated NAL");
    const auto sets = bytes({0, 0, 0, 1, 0x67, 0x42, 0, 0, 0, 1, 0x68, 0xce});
    auto idr = annexb;
    ensure_parameter_sets(idr, true, sets);
    check(idr.size() == annexb.size() + sets.size() && contains_nal(idr, nal_sps) && contains_nal(idr, nal_pps), "IDR gets SPS/PPS");
    const auto size = idr.size();
    ensure_parameter_sets(idr, true, sets);
    check(idr.size() == size, "SPS not duplicated");
    auto delta = bytes({0, 0, 0, 1, 0x41});
    ensure_parameter_sets(delta, false, sets);
    check(delta.size() == 5, "Non-keyframe untouched");

    // Letterbox and NV12 conversion (Windows encoder input).
    check(fit_letterbox(1920, 1080, 1920, 1080) == Letterbox{0, 0, 1920, 1080}, "Same aspect");
    check(fit_letterbox(1440, 1080, 1920, 1080) == Letterbox{240, 0, 1440, 1080}, "Pillarbox");
    check(fit_letterbox(2560, 1600, 1920, 1080) == Letterbox{96, 0, 1728, 1080}, "16:10 into 16:9");
    std::vector<std::uint8_t> white(8 * 4 * 4, 255);
    std::vector<std::uint8_t> nv12(16 * 4 * 3 / 2);
    bgra_to_nv12(white, 8, 4, 8 * 4, nv12, 16, 4);
    check(nv12[0] == 16 && nv12[4] == 235 && nv12[11] == 235 && nv12[12] == 16, "White picture with black bars");
    check(nv12[64 + 4] == 128 && nv12[64 + 5] == 128, "Neutral chroma");
    refused = false;
    try { bgra_to_nv12(white, 8, 4, 32, nv12, 15, 4); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, "Odd output size");

    // PCM framing: 44.1 kHz mono becomes 48 kHz stereo 20 ms frames.
    PcmFramer framer;
    std::vector<float> tone(4410);
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = std::sin(static_cast<float>(i) * 0.05f);
    std::size_t frames = 0;
    bool stereo_copy = true;
    std::int64_t last = -1;
    const auto sink = [&](std::span<const float> frame, std::int64_t ts) {
        check(frame.size() == static_cast<std::size_t>(pcm_frame_samples * pcm_channels), "Frame size");
        for (std::size_t i = 0; i < frame.size(); i += 2) stereo_copy &= frame[i] == frame[i + 1];
        check(last < 0 || ts == last + 20000, "Frame timestamps");
        last = ts;
        ++frames;
    };
    framer.push({tone, 1, 44100, 0}, sink);
    framer.push({tone, 1, 44100, 100000}, sink);
    // 200 ms of input; resampling keeps up to one output sample back for interpolation.
    check(frames >= 9 && frames <= 10 && stereo_copy, "200 ms of audio is about ten frames, mono duplicated");
    PcmFramer exact;
    std::vector<float> stereo(960 * 2 * 3, 0.25f);
    std::size_t exact_frames = 0;
    exact.push({stereo, 2, 48000, 0}, [&](std::span<const float> frame, std::int64_t) { exact_frames += frame[0] == 0.25f; });
    check(exact_frames == 3, "48 kHz stereo passes through");

    EventQueue queue;
    int ran = 0;
    queue.post([&] { ++ran; queue.post([&] { ran += 10; }); });
    check(queue.drain() == 1 && ran == 1, "Work posted while draining waits");
    check(queue.drain() == 1 && ran == 11, "Next drain runs it");

    // Settings round-trip and validation.
    PresenterSettings settings;
    settings.stream.max_bitrate_kbps = 4500;
    settings.overlay = {"top-left", 10, 20, true, -1200, 40};
    settings.comments_visible = false;
    check(settings_from_json(settings_to_json(settings)) == settings, "Settings round-trip");
    check(settings_from_json(Json::object()) == PresenterSettings{}, "Defaults");
    refused = false;
    try { settings_from_json({{"overlay", {{"corner", "centre"}}}}); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, "Invalid corner");
    refused = false;
    try { settings_from_json({{"stream", {{"fps", "thirty"}}}}); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, "Wrong type");
}); }
