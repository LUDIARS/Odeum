#pragma once
#include "../core/audio_source.hpp"
#include "../core/capture_source.hpp"
#include "../core/video_encoder.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace odeum::presenter {
// Everything the presenter needs from the OS besides the Tela surfaces. Each platform folder
// provides one; the application never names an OS API itself.
class Platform {
public:
    virtual ~Platform() = default;
    virtual std::unique_ptr<CaptureSource> capture() = 0;
    virtual std::unique_ptr<VideoEncoder> video_encoder() = 0;
    virtual std::unique_ptr<AudioSource> audio_source() = 0;
    // Plain text on the clipboard, UTF-8; empty when there is none.
    virtual std::string clipboard_text() = 0;
    // Per-user settings file (created on first save).
    virtual std::filesystem::path settings_path() = 0;
    // Fonts tried in order when the settings name none.
    virtual std::vector<std::filesystem::path> font_candidates() = 0;
};
}
