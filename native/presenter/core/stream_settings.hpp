#pragma once
#include <stdexcept>

namespace odeum::presenter {
// What the presenter streams. Defaults follow the design: 1080p, 30 fps, at most 6 Mbps,
// no audio. Every value can be overridden from the command line or the settings file.
struct StreamSettings {
    int width = 1920, height = 1080;
    int fps = 30;
    int max_bitrate_kbps = 6000;
    // An IDR at least this often, besides the ones the relay asks for.
    int keyframe_interval_s = 4;
    bool audio = false;
    int audio_bitrate_kbps = 64;
    bool operator==(const StreamSettings&) const = default;
};

// Throws std::invalid_argument for a size outside 320x180..3840x2160 or not even (H.264 4:2:0
// needs even dimensions), fps outside 1..60, bitrate outside 300..20000 kbps, keyframe interval
// outside 1..30 s or audio bitrate outside 16..256 kbps.
void validate(const StreamSettings&);
}
