#pragma once
#include "../../app/platform.hpp"

namespace odeum::presenter::macos {
// ScreenCaptureKit (video and system audio), VideoToolbox H.264 and NSPasteboard. Settings live
// in ~/Library/Application Support/Odeum/presenter.json. Requires macOS 12.3 or later.
class MacOSPlatform final : public Platform {
public:
    std::unique_ptr<CaptureSource> capture() override;
    std::unique_ptr<VideoEncoder> video_encoder() override;
    std::unique_ptr<AudioSource> audio_source() override;
    std::string clipboard_text() override;
    std::filesystem::path settings_path() override;
    std::vector<std::filesystem::path> font_candidates() override;
};
}
