#pragma once
#include "../../app/platform.hpp"

namespace odeum::presenter::windows {
// Windows Graphics Capture, the Media Foundation H.264 encoder, WASAPI loopback and the Win32
// clipboard. Settings live in %APPDATA%\Odeum\presenter.json.
class WindowsPlatform final : public Platform {
public:
    std::unique_ptr<CaptureSource> capture() override;
    std::unique_ptr<VideoEncoder> video_encoder() override;
    std::unique_ptr<AudioSource> audio_source() override;
    std::string clipboard_text() override;
    std::filesystem::path settings_path() override;
    std::vector<std::filesystem::path> font_candidates() override;
};
}
