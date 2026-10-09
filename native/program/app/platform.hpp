#pragma once
#include "../core/media_codecs.hpp"
#include "core/video_encoder.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace odeum::program {
// Everything odeum-program needs from the OS besides the Tela panel. Each platform folder
// provides one; the application never names an OS API itself.
class ProgramPlatform {
public:
    virtual ~ProgramPlatform() = default;
    virtual std::unique_ptr<VideoDecoder> video_decoder() = 0;
    virtual std::unique_ptr<AacEncoder> aac_encoder() = 0;
    // The H.264 encoder of odeum_sender (VideoToolbox / Media Foundation), used twice: the
    // programme (High, B-frames) and the return feed (Constrained Baseline).
    virtual std::unique_ptr<presenter::VideoEncoder> video_encoder() = 0;
    virtual std::unique_ptr<SecretStore> secret_store() = 0;
    // Plain text on the clipboard, UTF-8; empty when there is none.
    virtual std::string clipboard_text() = 0;
    // Per-user settings file (created on first save).
    virtual std::filesystem::path settings_path() = 0;
    // Fonts tried in order when the settings name none.
    virtual std::vector<std::filesystem::path> font_candidates() = 0;
};
}
