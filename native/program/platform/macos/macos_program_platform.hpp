#pragma once
#include "../../app/platform.hpp"

namespace odeum::program::macos {
// VideoToolbox decoding and H.264 encoding, AudioToolbox AAC-LC, the Keychain for the stream key
// and the general pasteboard. Settings live in ~/Library/Application Support/Odeum/program.json.
class MacOSProgramPlatform final : public ProgramPlatform {
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
