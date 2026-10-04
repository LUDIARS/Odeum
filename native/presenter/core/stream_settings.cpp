#include "stream_settings.hpp"

namespace odeum::presenter {
void validate(const StreamSettings& s) {
    if (s.width < 320 || s.width > 3840 || s.height < 180 || s.height > 2160 || s.width % 2 || s.height % 2)
        throw std::invalid_argument("Stream size must be even and between 320x180 and 3840x2160");
    if (s.fps < 1 || s.fps > 60) throw std::invalid_argument("Frame rate must be 1..60");
    if (s.max_bitrate_kbps < 300 || s.max_bitrate_kbps > 20000) throw std::invalid_argument("Bitrate must be 300..20000 kbps");
    if (s.keyframe_interval_s < 1 || s.keyframe_interval_s > 30) throw std::invalid_argument("Keyframe interval must be 1..30 s");
    if (s.audio_bitrate_kbps < 16 || s.audio_bitrate_kbps > 256) throw std::invalid_argument("Audio bitrate must be 16..256 kbps");
}
}
