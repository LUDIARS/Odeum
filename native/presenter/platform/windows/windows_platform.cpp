#include "windows_platform.hpp"
#include "graphics_capture_source.hpp"
#include "media_foundation_encoder.hpp"
#include "wasapi_loopback_source.hpp"
#include "windows_shell.hpp"

namespace odeum::presenter::windows {
std::unique_ptr<CaptureSource> WindowsPlatform::capture() { return std::make_unique<GraphicsCaptureSource>(); }
std::unique_ptr<VideoEncoder> WindowsPlatform::video_encoder() { return std::make_unique<MediaFoundationEncoder>(); }
std::unique_ptr<AudioSource> WindowsPlatform::audio_source() { return std::make_unique<WasapiLoopbackSource>(); }
std::string WindowsPlatform::clipboard_text() { return windows::clipboard_text(); }
std::filesystem::path WindowsPlatform::settings_path() { return roaming_app_data() / L"Odeum" / L"presenter.json"; }

std::vector<std::filesystem::path> WindowsPlatform::font_candidates() {
    std::vector<std::filesystem::path> fonts{local_app_data() / L"Odeum" / L"presenter.ttf"};
    for (auto& font : system_font_candidates()) fonts.push_back(std::move(font));
    return fonts;
}
}
