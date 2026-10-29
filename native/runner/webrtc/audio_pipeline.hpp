#pragma once
// Process audio for WebRTC (audio-pipeline.ts, audio-source.ts, audio-target.ts, process-loopback.ts):
// ProcessLoopback.exe <pid> (Windows Application Loopback; built from custos' C# source with the
// system csc.exe on first use, cached by source hash) -> s16le PCM -> ffmpeg Opus -> RTP.
// The target pid is the bridge /health pid, else the pid of the runner's own `run` process.
#include "../adapters/media_tools.hpp"
#include "rtp_source_pool.hpp"

namespace odeum::runner {
struct AudioSettings {
    std::string loopback_source;  // ProcessLoopback.cs
    std::string windir;           // csc.exe location
    std::string temp_dir;         // built helper cache
};
class LoopbackHelper;
// Owns the helper build state shared by every audio source.
class AudioPipelineFactory {
public:
    AudioPipelineFactory(boost::asio::io_context& io, MediaTools& media, AudioSettings settings, std::function<std::optional<long long>(const std::string&)> run_pid);
    ~AudioPipelineFactory();
    std::unique_ptr<RtpPipeline> make();
private:
    boost::asio::io_context& io_;
    MediaTools& media_;
    std::shared_ptr<LoopbackHelper> helper_;
    std::function<std::optional<long long>(const std::string&)> run_pid_;
};
}
