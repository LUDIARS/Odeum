#pragma once
#include "../../app/platform.hpp"

namespace odeum::program::windows {
// Media Foundation H.264 decoding, AAC-LC and H.264 encoding, DPAPI for the stream key and the
// Win32 clipboard. Settings live in %APPDATA%\Odeum\program.json.
class WindowsProgramPlatform final : public ProgramPlatform {
public:
    std::unique_ptr<VideoDecoder> video_decoder() override;
    std::unique_ptr<AacEncoder> aac_encoder() override;
    std::unique_ptr<presenter::VideoEncoder> video_encoder() override;
    std::unique_ptr<SecretStore> secret_store() override;
    std::string clipboard_text() override;
    std::filesystem::path settings_path() override;
    std::vector<std::filesystem::path> font_candidates() override;
};
}
