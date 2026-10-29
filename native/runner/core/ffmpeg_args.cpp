#include "ffmpeg_args.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace odeum::runner {
long long js_round(double value) { return static_cast<long long>(std::floor(value + 0.5)); }

const char* encoder_name(Encoder encoder) {
    switch (encoder) {
        case Encoder::nvenc: return "nvenc";
        case Encoder::x264: return "x264";
        case Encoder::amf: return "amf";
        case Encoder::qsv: return "qsv";
    }
    return "x264";
}

namespace {
const char* codec(Encoder encoder) {
    switch (encoder) {
        case Encoder::nvenc: return "h264_nvenc";
        case Encoder::amf: return "h264_amf";
        case Encoder::qsv: return "h264_qsv";
        case Encoder::x264: return "libx264";
    }
    return "libx264";
}

std::vector<std::string> concat(std::vector<std::string> a, const std::vector<std::string>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::string rtp_url(const std::string& host, int port) { return "rtp://" + host + ":" + std::to_string(port) + "?pkt_size=1200"; }
}

std::optional<Encoder> forced_encoder(std::string_view value) {
    std::string v(value);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "nvenc") return Encoder::nvenc;
    if (v == "x264") return Encoder::x264;
    if (v == "amf") return Encoder::amf;
    if (v == "qsv") return Encoder::qsv;
    return std::nullopt;
}

std::vector<Encoder> probe_order() { return {Encoder::nvenc, Encoder::qsv, Encoder::amf, Encoder::x264}; }

std::vector<std::string> probe_args(Encoder encoder) {
    return {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=red:size=320x240:rate=10", "-frames:v", "5",
        "-pix_fmt", "yuv420p", "-c:v", codec(encoder), "-f", "null", "-"};
}

std::vector<std::string> encoder_args(Encoder encoder, int fps, long long bitrate_bps) {
    const auto gop = std::to_string(std::max(2, fps));
    const auto kbps = js_round(static_cast<double>(bitrate_bps) / 1000);
    const auto max = js_round(static_cast<double>(kbps) * 1.5);
    const auto k = std::to_string(kbps) + "k", m = std::to_string(max) + "k";
    switch (encoder) {
        case Encoder::nvenc:
            return {"-c:v", "h264_nvenc", "-preset", "p4", "-tune", "ll", "-rc", "vbr", "-cq", "28", "-b:v", k, "-maxrate", m,
                "-profile:v", "baseline", "-pix_fmt", "yuv420p", "-bf", "0", "-g", gop};
        case Encoder::amf:
            return {"-c:v", "h264_amf", "-usage", "ultralowlatency", "-quality", "speed", "-rc", "vbr_peak", "-b:v", k, "-maxrate", m,
                "-profile:v", "baseline", "-pix_fmt", "yuv420p", "-bf", "0", "-g", gop};
        case Encoder::qsv:
            return {"-c:v", "h264_qsv", "-preset", "veryfast", "-b:v", k, "-maxrate", m, "-profile:v", "baseline", "-pix_fmt", "yuv420p",
                "-bf", "0", "-g", gop};
        case Encoder::x264:
            break;
    }
    return {"-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency", "-profile:v", "baseline", "-level", "3.1", "-pix_fmt", "yuv420p",
        "-b:v", k, "-maxrate", m, "-bufsize", k, "-bf", "0", "-g", gop};
}

Platform current_platform() {
#if defined(_WIN32)
    return Platform::windows_os;
#elif defined(__APPLE__)
    return Platform::macos;
#elif defined(__linux__)
    return Platform::linux_os;
#else
    return Platform::other;
#endif
}

std::vector<std::string> screenshot_args(const CaptureConfig& capture, const CaptureHost& host) {
    const std::vector<std::string> out{"-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "-"};
    switch (host.platform) {
        case Platform::windows_os:
            if (capture.type == "window" && capture.window_title && !capture.window_title->empty())
                return concat({"-loglevel", "error", "-f", "gdigrab", "-framerate", "1", "-i", "title=" + *capture.window_title}, out);
            return concat({"-loglevel", "error", "-f", "gdigrab", "-framerate", "1", "-i", "desktop"}, out);
        case Platform::linux_os:
            return concat({"-loglevel", "error", "-f", "x11grab", "-framerate", "1", "-i", host.display}, out);
        case Platform::macos:
            return concat({"-loglevel", "error", "-f", "avfoundation", "-framerate", "1", "-i", host.avfoundation_input}, out);
        case Platform::other:
            break;
    }
    throw std::runtime_error("unsupported platform for screenshot");
}

std::vector<std::string> capture_rtp_args(const CaptureConfig& capture, const CaptureHost& host, Encoder encoder, long long bitrate_bps,
    const std::string& rtp_host, int rtp_port, int payload_type) {
    const auto fps = std::to_string(capture.fps);
    const auto out = concat(concat({"-an"}, encoder_args(encoder, capture.fps, bitrate_bps)),
        {"-payload_type", std::to_string(payload_type), "-f", "rtp", rtp_url(rtp_host, rtp_port)});
    switch (host.platform) {
        case Platform::windows_os:
            if (capture.type == "window" && capture.window_title && !capture.window_title->empty())
                return concat({"-loglevel", "warning", "-f", "gdigrab", "-framerate", fps, "-i", "title=" + *capture.window_title}, out);
            return concat({"-loglevel", "warning", "-f", "gdigrab", "-framerate", fps, "-i", "desktop"}, out);
        case Platform::linux_os:
            return concat({"-loglevel", "warning", "-f", "x11grab", "-framerate", fps, "-i", host.display}, out);
        case Platform::macos:
            return concat({"-loglevel", "warning", "-f", "avfoundation", "-framerate", fps, "-i", host.avfoundation_input}, out);
        case Platform::other:
            break;
    }
    return concat({"-loglevel", "warning", "-f", "lavfi", "-i", "testsrc=size=640x480:rate=" + fps}, out);
}

std::vector<std::string> bridge_stream_args(const BridgeStreamMeta& meta, Encoder encoder, long long bitrate_bps, int rtp_port,
    const std::string& rtp_host, int payload_type) {
    return concat(concat({"-loglevel", "warning", "-use_wallclock_as_timestamps", "1", "-f", "rawvideo", "-pix_fmt", meta.pixfmt,
        "-s", std::to_string(meta.width) + "x" + std::to_string(meta.height), "-r", std::to_string(meta.fps), "-i", "pipe:0", "-an",
        "-force_key_frames", "expr:gte(t,n_forced*1)", "-fps_mode", "passthrough", "-vf", "crop=trunc(iw/2)*2:trunc(ih/2)*2"},
        encoder_args(encoder, meta.fps, bitrate_bps)), {"-payload_type", std::to_string(payload_type), "-f", "rtp", rtp_url(rtp_host, rtp_port)});
}

std::vector<std::string> audio_args(int rtp_port, const std::string& rtp_host) {
    return {"-loglevel", "warning", "-f", "s16le", "-ar", "48000", "-ac", "2", "-i", "pipe:0", "-vn", "-c:a", "libopus", "-b:a", "96k",
        "-application", "lowdelay", "-frame_duration", "20", "-payload_type", std::to_string(opus_payload_type), "-f", "rtp", rtp_url(rtp_host, rtp_port)};
}

std::vector<std::string> scale_args(long long max_width) {
    return {"-hide_banner", "-loglevel", "error", "-f", "image2pipe", "-vcodec", "png", "-i", "pipe:0",
        "-vf", "scale='min(" + std::to_string(max_width) + ",iw)':-1:flags=fast_bilinear", "-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "pipe:1"};
}

std::optional<std::uint32_t> read_png_width(std::string_view png) {
    if (png.size() < 24 || !looks_like_png(png)) return std::nullopt;
    const auto b = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(png[i])); };
    return (b(16) << 24) | (b(17) << 16) | (b(18) << 8) | b(19);
}
}
