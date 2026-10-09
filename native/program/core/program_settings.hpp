#pragma once
#include "program_state.hpp"
#include "core/video_encoder.hpp"
#include <odeum/message.hpp>
#include <string>
#include <string_view>

namespace odeum::program {
// Fixed by the design: the programme is 1080p30, the return feed 640x360 at 30 fps.
inline constexpr int program_width = 1920, program_height = 1080, program_fps = 30;
inline constexpr int return_width = 640, return_height = 360;
inline constexpr int aac_rate = 48000, aac_channels = 2;

// odeum-program's per-user settings (JSON). The YouTube stream key is not here: it lives in
// the OS secret store (Keychain / DPAPI).
struct ProgramSettings {
    int video_bitrate_kbps = 8000;  // 6000..10000, CBR
    int keyframe_interval_s = 2;    // 1..4 (YouTube asks for 2)
    presenter::H264Profile profile = presenter::H264Profile::high; // main or high
    int b_frames = 2;               // 0..2
    int audio_bitrate_kbps = 128;   // AAC-LC, 64..320
    int return_bitrate_kbps = 1200; // 300..2500, Constrained Baseline
    int return_audio_kbps = 64;     // Opus, 16..256
    std::string youtube_url = "rtmps://a.rtmps.youtube.com:443/live2";
    std::string font;               // TrueType for the panel and the reaction layer
    double volume = 1.0;            // 0..2
    std::array<bool, input_count> muted{};
    bool operator==(const ProgramSettings&) const = default;
};

// Throws std::invalid_argument naming the field when a value is outside its range.
void validate(const ProgramSettings&);
ProgramSettings settings_from_json(const Json&);
Json settings_to_json(const ProgramSettings&);

struct RtmpEndpoint {
    std::string host;
    int port{};
    std::string app;
    std::string tc_url; // rtmps://host:port/app
    bool operator==(const RtmpEndpoint&) const = default;
};
// rtmps://host[:port]/app with no credentials, query or further path. Port defaults to 443.
// Throws std::invalid_argument otherwise (plain rtmp:// is refused: the key would travel in clear).
RtmpEndpoint parse_rtmps_url(std::string_view url);

// A YouTube stream key: 1..128 characters of A-Z a-z 0-9 '-' '_'. Throws std::invalid_argument
// with a message that never repeats the key.
void validate_stream_key(std::string_view key);
}
