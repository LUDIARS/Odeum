#pragma once
// ffmpeg command lines (argument arrays; never a shell string) of the former capture modules:
// screenshot.ts, scale.ts, ffmpeg-pipeline.ts and audio-pipeline.ts.
#include "app_config.hpp"
#include "bridge_contract.hpp"
#include <optional>
#include <string>
#include <vector>

namespace odeum::runner {
enum class Encoder { nvenc, x264, amf, qsv };
const char* encoder_name(Encoder encoder);
// CUSTOS_VIDEO_ENCODER: nvenc | x264 | amf | qsv forces one (case-insensitive); anything else probes.
std::optional<Encoder> forced_encoder(std::string_view value);
// Probe order of "auto": hardware first, libx264 last.
std::vector<Encoder> probe_order();
std::vector<std::string> probe_args(Encoder encoder);
std::vector<std::string> encoder_args(Encoder encoder, int fps, long long bitrate_bps);

enum class Platform { windows_os, linux_os, macos, other };
Platform current_platform();

struct CaptureHost {
    Platform platform;
    std::string display;             // x11grab input
    std::string avfoundation_input;  // avfoundation input
};
// One PNG frame on stdout. Throws std::runtime_error on an unsupported platform.
std::vector<std::string> screenshot_args(const CaptureConfig& capture, const CaptureHost& host);
// Desktop capture -> H.264 -> RTP (pkt_size 1200) to rtp_host:rtp_port.
std::vector<std::string> capture_rtp_args(const CaptureConfig& capture, const CaptureHost& host, Encoder encoder, long long bitrate_bps,
    const std::string& rtp_host, int rtp_port, int payload_type);
// Bridge raw frames on stdin -> H.264 -> RTP.
std::vector<std::string> bridge_stream_args(const BridgeStreamMeta& meta, Encoder encoder, long long bitrate_bps, int rtp_port,
    const std::string& rtp_host, int payload_type);
inline constexpr int opus_payload_type = 111;
// s16le 48 kHz stereo PCM on stdin -> Opus -> RTP.
std::vector<std::string> audio_args(int rtp_port, const std::string& rtp_host = "127.0.0.1");
// PNG on stdin -> PNG no wider than max_width on stdout.
std::vector<std::string> scale_args(long long max_width);
// IHDR width of a PNG; nullopt for anything else.
std::optional<std::uint32_t> read_png_width(std::string_view png);
long long js_round(double value);
}
