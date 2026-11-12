#pragma once
// Video sources of a WebRTC session (ffmpeg-pipeline.ts, h264-bridge-pipeline.ts, video-pipeline-selector.ts):
//  - desktop capture: ffmpeg gdigrab / x11grab / avfoundation -> H.264 -> RTP (UDP loopback)
//  - in-app bridge raw frames: GET /stream (BGRA) -> ffmpeg stdin -> RTP, with back-pressure
//  - Unity bridge hardware H.264: GET /stream?codec=h264 -> RTP in process (no ffmpeg); key frame
//    requests and bitrate changes go back to the bridge
// The selector tries H.264 first for Unity bridges and falls back to raw frames on 415 / 404.
#include "../adapters/media_tools.hpp"
#include "rtp_source_pool.hpp"

namespace odeum::runner {
struct VideoSettings {
    long long bitrate = 4'000'000;  // CUSTOS_VIDEO_BITRATE (ffmpeg paths)
};
std::unique_ptr<RtpPipeline> make_video_pipeline(boost::asio::io_context& io, MediaTools& media, VideoSettings settings);
// ffmpeg stderr lines worth a warning (the former /error|Error|failed|Could not/i filter).
bool is_ffmpeg_problem(std::string_view line);
}
