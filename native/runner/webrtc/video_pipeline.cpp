#include "video_pipeline.hpp"
#include "../adapters/bridge_client.hpp"
#include "../adapters/clock.hpp"
#include "../adapters/http_client.hpp"
#include "../adapters/log.hpp"
#include "../adapters/process.hpp"
#include "../core/h264.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace odeum::runner {
namespace {
constexpr std::size_t stdin_pause_bytes = 16u * 1024 * 1024;   // raw frames waiting for ffmpeg: pause the bridge
constexpr std::size_t stdin_resume_bytes = 4u * 1024 * 1024;
constexpr auto connect_timeout = std::chrono::milliseconds(10'000);

std::string spawn_message(const std::string& kind, const std::string& error) {
    return kind + " のパイプラインを起動できません (" + error + ")。ffmpeg は PATH に入れるか CUSTOS_FFMPEG で実行ファイルを指定してください";
}

// stderr chunks -> trimmed text, warning lines forwarded.
std::function<void(std::string_view)> stderr_sink(std::function<void(const std::string&)> forward) {
    return [forward](std::string_view chunk) {
        auto text = sanitize_utf8(chunk);
        const auto begin = text.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return;
        text = text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
        if (is_ffmpeg_problem(text)) {
            log::warn("ffmpeg-capture", "ffmpeg stderr", {{"msg", text.substr(0, 2000)}});
            if (forward) forward(text.substr(0, 2000));
        } else {
            log::debug("ffmpeg-capture", "ffmpeg stderr", {{"msg", text.substr(0, 2000)}});
        }
    };
}

// Desktop capture: ffmpeg grabs the screen / window and sends RTP to the pool's UDP port.
class CapturePipeline final : public RtpPipeline {
public:
    CapturePipeline(boost::asio::io_context& io, MediaTools& media, VideoSettings settings) : io_(io), media_(media), settings_(settings) {}
    void start(const AppConfig& app, int rtp_port, Events events, std::function<void(std::optional<StartError>)> started) override {
        if (!app.capture) return started(StartError{"app " + app.id + " has no capture config"});
        events_ = std::move(events);
        auto alive = alive_;
        media_.encoder([this, alive, app, rtp_port, started](Encoder encoder) {
            if (!*alive) return started(StartError{"stopped"});
            ProcessSpec spec{media_.ffmpeg(), capture_rtp_args(*app.capture, media_.host(), encoder, settings_.bitrate, "127.0.0.1", rtp_port, 96), {}, {}, false, false, true, false};
            ChildProcess::Handlers handlers;
            handlers.on_stderr = stderr_sink(events_.stderr_line);
            handlers.on_exit = [this, alive](const ExitStatus& s) {
                log::info("ffmpeg-capture", "ffmpeg exited", {{"code", s.code ? Json(*s.code) : Json(nullptr)}, {"signal", s.signal ? Json(*s.signal) : Json(nullptr)}});
                if (*alive && events_.exit) events_.exit();
            };
            try {
                process_ = ChildProcess::spawn(io_, spec, std::move(handlers));
            } catch (const SpawnError& error) {
                return started(StartError{spawn_message("video", error.what())});
            }
            log::info("ffmpeg-capture", "spawn ffmpeg (rtp)", {{"appId", app.id}});
            started(std::nullopt);
        });
    }
    void stop() override {
        *alive_ = false;
        if (process_) process_->terminate();
        process_.reset();
    }
private:
    boost::asio::io_context& io_;
    MediaTools& media_;
    VideoSettings settings_;
    Events events_;
    std::shared_ptr<ChildProcess> process_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// Raw BGRA frames from the bridge piped into ffmpeg. The HTTP read pauses while ffmpeg is behind,
// so frames are never cut (rawvideo needs whole frames).
class RawBridgePipeline final : public RtpPipeline {
public:
    RawBridgePipeline(boost::asio::io_context& io, MediaTools& media, VideoSettings settings) : io_(io), media_(media), settings_(settings) {}
    void start(const AppConfig& app, int rtp_port, Events events, std::function<void(std::optional<StartError>)> started) override {
        events_ = std::move(events);
        const auto& bridge = *app.in_app_bridge;
        const BridgeEndpoint ep{bridge.host, bridge.port};
        auto alive = alive_;
        auto start_once = std::make_shared<std::function<void(std::optional<StartError>)>>(std::move(started));
        auto settle = [start_once](std::optional<StartError> e) { if (*start_once) { auto f = std::move(*start_once); *start_once = nullptr; f(std::move(e)); } };
        HttpStream::Handlers handlers;
        handlers.on_headers = [this, alive, settle, rtp_port, fps = bridge.fps](unsigned status, const std::map<std::string, std::string>& headers) {
            if (status != 200) { settle(StartError{"bridge /stream returned HTTP " + std::to_string(status) + " — expected 200"}); return false; }
            const auto meta = parse_frame_headers(headers, fps);
            if (!meta) {
                settle(StartError{"bridge /stream response is missing required X-Frame-* headers (X-Frame-Width, X-Frame-Height, X-Frame-Pixfmt, X-Frame-Fps)"});
                return false;
            }
            stream_->pause();  // until ffmpeg runs
            log::info("ffmpeg-capture", "bridge stream connected", {{"meta", Json{{"width", meta->width}, {"height", meta->height}, {"pixfmt", meta->pixfmt}, {"fps", meta->fps}}}});
            media_.encoder([this, alive, settle, rtp_port, meta = *meta](Encoder encoder) {
                if (!*alive) return settle(StartError{"stopped"});
                ProcessSpec spec{media_.ffmpeg(), bridge_stream_args(meta, encoder, settings_.bitrate, rtp_port, "127.0.0.1", 96), {}, {}, true, false, true, false};
                ChildProcess::Handlers h;
                h.on_stderr = stderr_sink(events_.stderr_line);
                h.on_exit = [this, alive](const ExitStatus&) { if (*alive) finish("ffmpeg exited"); };
                try {
                    process_ = ChildProcess::spawn(io_, spec, std::move(h));
                } catch (const SpawnError& error) {
                    settle(StartError{spawn_message("video", error.what())});  // before cancel(): its end notice must not win
                    if (stream_) stream_->cancel();
                    return;
                }
                running_ = true;
                settle(std::nullopt);
                stream_->resume();
            });
            return true;
        };
        handlers.on_data = [this, alive](std::string_view chunk) {
            if (!*alive || !process_) return false;
            process_->write_stdin(std::make_shared<const std::string>(chunk), [this, alive](bool ok) {
                if (!*alive) return;
                if (!ok) return finish("ffmpeg stdin closed");
                if (process_ && stream_ && process_->stdin_queued_bytes() < stdin_resume_bytes) stream_->resume();
            });
            if (process_->stdin_queued_bytes() > stdin_pause_bytes) stream_->pause();
            return true;
        };
        handlers.on_end = [this, alive, settle](const std::optional<HttpError>& error) {
            if (!*alive) return;
            if (!running_) return settle(StartError{error ? error->message : std::string("bridge /stream ended before the first frame")});
            log::info("ffmpeg-capture", error ? "bridge stream error — triggering session teardown" : "bridge stream ended — triggering session teardown");
            finish("bridge stream ended");
        };
        stream_ = HttpStream::open(io_, ep.base_url() + "/stream?fps=" + std::to_string(bridge.fps), connect_timeout, 500, std::move(handlers));
    }
    void stop() override {
        *alive_ = false;
        if (stream_) stream_->cancel();
        if (process_) process_->terminate();
        stream_.reset();
        process_.reset();
    }
private:
    boost::asio::io_context& io_;
    MediaTools& media_;
    VideoSettings settings_;
    Events events_;
    std::shared_ptr<HttpStream> stream_;
    std::shared_ptr<ChildProcess> process_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    bool running_ = false, finished_ = false;
    void finish(const char* why) {
        if (finished_) return;
        finished_ = true;
        log::info("ffmpeg-capture", why);
        if (events_.exit) events_.exit();
    }
};

// Hardware H.264 from the Unity bridge, packetized here (RFC 6184, PT 96, MTU 1200).
class H264BridgePipeline final : public RtpPipeline {
public:
    explicit H264BridgePipeline(boost::asio::io_context& io) : io_(io) {}
    bool takes_feedback() const override { return true; }
    void start(const AppConfig& app, int, Events events, std::function<void(std::optional<StartError>)> started) override {
        events_ = std::move(events);
        const auto& bridge = *app.in_app_bridge;
        endpoint_ = BridgeEndpoint{bridge.host, bridge.port};
        packetizer_.emplace(96, rtp_mtu, static_cast<std::uint16_t>(random_below(0x10000)), 1 + random_below(0x7ffffffe));
        auto alive = alive_;
        auto start_once = std::make_shared<std::function<void(std::optional<StartError>)>>(std::move(started));
        auto settle = [start_once](std::optional<StartError> e) { if (*start_once) { auto f = std::move(*start_once); *start_once = nullptr; f(std::move(e)); } };
        const auto query = form_encode({{"codec", "h264"}, {"fps", std::to_string(bridge.fps)}, {"width", std::to_string(bridge.video.width)},
            {"height", std::to_string(bridge.video.height)}, {"bitrate", std::to_string(bridge.video.bitrate)}});
        auto status = std::make_shared<unsigned>(0);
        HttpStream::Handlers handlers;
        handlers.on_headers = [this, settle, status](unsigned code, const std::map<std::string, std::string>& headers) {
            *status = code;
            if (code == 415 || code == 404) return false;  // body read for the message, then fall back
            const auto codec = headers.find("x-frame-codec");
            if (code != 200 || codec == headers.end() || codec->second != "h264") {
                settle(StartError{"bridge /stream?codec=h264 returned HTTP " + std::to_string(code) + " codec=" + (codec == headers.end() ? std::string("-") : codec->second)});
                return false;
            }
            running_ = true;
            log::info("h264-bridge", "bridge h264 stream connected", {{"host", endpoint_.host}, {"port", endpoint_.port}});
            settle(std::nullopt);
            return true;
        };
        handlers.on_data = [this, alive](std::string_view chunk) {
            if (!*alive) return false;
            try {
                for (const auto& frame : reader_.push(chunk)) {
                    for (const auto& packet : packetizer_->packetize(frame.data, frame.timestamp_us)) if (events_.packet) events_.packet(packet);
                    if (++frames_ == 1) log::info("h264-bridge", "first h264 frame from bridge", {{"keyframe", frame.keyframe}, {"bytes", frame.data.size()}});
                }
            } catch (const FrameTooLarge& error) {
                if (events_.stderr_line) events_.stderr_line(std::string("h264 stream parse error: ") + error.what());
                finish();
                return false;
            }
            return true;
        };
        handlers.on_end = [this, alive, settle, status](const std::optional<HttpError>& error) {
            if (!*alive) return;
            if (!running_) {
                if (*status == 415 || *status == 404) {
                    return settle(StartError{"bridge cannot stream h264 (HTTP " + std::to_string(*status) + "): " + stream_->error_body().substr(0, 500), true});
                }
                return settle(StartError{error ? error->message : std::string("bridge /stream?codec=h264 ended")});
            }
            log::info("h264-bridge", error ? "bridge h264 stream error" : "bridge h264 stream ended");
            finish();
        };
        stream_ = HttpStream::open(io_, endpoint_.base_url() + "/stream?" + query, connect_timeout, 500, std::move(handlers));
    }
    void request_keyframe() override {
        if (!running_ || ended_) return;
        bridge_post_control(io_, endpoint_, "/stream/keyframe", "{}", [](std::optional<std::string> error) {
            if (error) log::warn("h264-bridge", "keyframe request failed", {{"err", *error}});
        });
    }
    void set_bitrate(long long bps) override {
        if (!running_ || ended_) return;
        log::info("h264-bridge", "set bridge bitrate", {{"bps", bps}});
        bridge_post_control(io_, endpoint_, "/stream/bitrate", dump(Json{{"bps", bps}}), [bps](std::optional<std::string> error) {
            if (error) log::warn("h264-bridge", "bitrate change failed", {{"err", *error}, {"bps", bps}});
        });
    }
    void stop() override {
        *alive_ = false;
        ended_ = true;
        if (stream_) stream_->cancel();
        stream_.reset();
    }
private:
    boost::asio::io_context& io_;
    Events events_;
    BridgeEndpoint endpoint_;
    std::optional<H264RtpPacketizer> packetizer_;
    BridgeH264FrameReader reader_;
    std::shared_ptr<HttpStream> stream_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    bool running_ = false, ended_ = false;
    std::size_t frames_ = 0;
    void finish() {
        if (ended_) return;
        ended_ = true;
        if (events_.exit) events_.exit();
    }
};

class SelectorPipeline final : public RtpPipeline {
public:
    SelectorPipeline(boost::asio::io_context& io, MediaTools& media, VideoSettings settings) : io_(io), media_(media), settings_(settings) {}
    bool takes_feedback() const override { return h264_; }
    void request_keyframe() override { if (h264_ && inner_) inner_->request_keyframe(); }
    void set_bitrate(long long bps) override { if (h264_ && inner_) inner_->set_bitrate(bps); }
    void stop() override { if (inner_) inner_->stop(); }
    void start(const AppConfig& app, int rtp_port, Events events, std::function<void(std::optional<StartError>)> started) override {
        if (!app.in_app_bridge) {
            inner_ = std::make_unique<CapturePipeline>(io_, media_, settings_);
            return inner_->start(app, rtp_port, std::move(events), std::move(started));
        }
        const auto codec = app.in_app_bridge->kind == "unity" ? app.in_app_bridge->video.codec : std::string("rawvideo");
        if (codec == "rawvideo") return start_raw(app, rtp_port, std::move(events), std::move(started));
        inner_ = std::make_unique<H264BridgePipeline>(io_);
        inner_->start(app, rtp_port, events, [this, app, rtp_port, events, started, codec](std::optional<StartError> error) {
            if (!error) { h264_ = true; return started(std::nullopt); }
            inner_.reset();
            if (!error->h264_unavailable || codec == "h264") return started(error);
            log::info("video-pipeline", "bridge h264 unavailable — falling back to BGRA + ffmpeg", {{"reason", error->message}});
            start_raw(app, rtp_port, events, started);
        });
    }
private:
    boost::asio::io_context& io_;
    MediaTools& media_;
    VideoSettings settings_;
    std::unique_ptr<RtpPipeline> inner_;
    bool h264_ = false;
    void start_raw(const AppConfig& app, int rtp_port, Events events, std::function<void(std::optional<StartError>)> started) {
        inner_ = std::make_unique<RawBridgePipeline>(io_, media_, settings_);
        inner_->start(app, rtp_port, std::move(events), std::move(started));
    }
};
}

bool is_ffmpeg_problem(std::string_view line) {
    std::string lower(line);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("error") != std::string::npos || lower.find("failed") != std::string::npos || lower.find("could not") != std::string::npos;
}

std::unique_ptr<RtpPipeline> make_video_pipeline(boost::asio::io_context& io, MediaTools& media, VideoSettings settings) {
    return std::make_unique<SelectorPipeline>(io, media, settings);
}
}
