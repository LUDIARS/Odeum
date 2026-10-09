#include "windows_program_platform.hpp"
#include "dpapi_secret_store.hpp"
#include "media_foundation_aac_encoder.hpp"
#include "media_foundation_decoder.hpp"
#include "platform/windows/media_foundation_encoder.hpp"
#include "platform/windows/windows_shell.hpp"

namespace odeum::program::windows {
namespace shell = presenter::windows;

std::unique_ptr<VideoDecoder> WindowsProgramPlatform::video_decoder() { return std::make_unique<MediaFoundationDecoder>(); }
std::unique_ptr<AacEncoder> WindowsProgramPlatform::aac_encoder() { return std::make_unique<MediaFoundationAacEncoder>(); }
std::unique_ptr<presenter::VideoEncoder> WindowsProgramPlatform::video_encoder() {
    return std::make_unique<presenter::windows::MediaFoundationEncoder>();
}
std::unique_ptr<SecretStore> WindowsProgramPlatform::secret_store() {
    return std::make_unique<DpapiSecretStore>(shell::roaming_app_data() / L"Odeum" / L"program-stream-key.bin");
}
std::string WindowsProgramPlatform::clipboard_text() { return shell::clipboard_text(); }
std::filesystem::path WindowsProgramPlatform::settings_path() { return shell::roaming_app_data() / L"Odeum" / L"program.json"; }

std::vector<std::filesystem::path> WindowsProgramPlatform::font_candidates() {
    std::vector<std::filesystem::path> fonts{shell::local_app_data() / L"Odeum" / L"program.ttf"};
    for (auto& font : shell::system_font_candidates()) fonts.push_back(std::move(font));
    return fonts;
}
}
