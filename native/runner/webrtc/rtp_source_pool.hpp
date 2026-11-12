#pragma once
// One media source per app shared by every WebRTC session of that app (shared-rtp-source.ts):
// started by the first subscriber, stopped after the last; ffmpeg sends RTP to a loopback UDP port,
// in-process sources (bridge H.264) hand packets over directly. Viewers' PLI / REMB are combined by
// VideoFeedbackPolicy before reaching a source that can act on them.
#include "../core/app_config.hpp"
#include "../core/video_feedback_policy.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <variant>

namespace odeum::runner {
struct StartError {
    std::string message;
    bool h264_unavailable = false;  // the bridge cannot encode H.264 (415 / 404)
};

class RtpPipeline {
public:
    struct Events {
        std::function<void(std::string_view packet)> packet;    // in-process RTP
        std::function<void()> exit;                             // ended after a successful start
        std::function<void(const std::string& line)> stderr_line;
    };
    virtual ~RtpPipeline() = default;
    // `started(nullopt)` once running, `started(error)` when it could not start (nothing left running).
    virtual void start(const AppConfig& app, int rtp_port, Events events, std::function<void(std::optional<StartError>)> started) = 0;
    virtual void stop() = 0;
    virtual bool takes_feedback() const { return false; }
    virtual void request_keyframe() {}
    virtual void set_bitrate(long long) {}
};
using PipelineFactory = std::function<std::unique_ptr<RtpPipeline>()>;

struct RtpSink {
    std::function<void(std::string_view packet)> packet;
    std::function<void()> end;
};

class SharedRtpSourcePool {
public:
    SharedRtpSourcePool(boost::asio::io_context& io, PipelineFactory factory, std::string kind, std::function<long long()> now_ms);
    ~SharedRtpSourcePool();
    // done(rtp port) once the source runs, or done(error message).
    void acquire(const AppConfig& app, const std::string& subscriber, RtpSink sink, std::function<void(std::variant<int, std::string>)> done);
    void picture_loss(const std::string& app_id, const std::string& subscriber);
    void estimate(const std::string& app_id, const std::string& subscriber, double bps);
    void release(const std::string& app_id, const std::string& subscriber);
    std::vector<std::string> stderr_tail(const std::string& app_id) const;
    std::size_t subscribers(const std::string& app_id) const;
    void shutdown();
private:
    struct Source;
    boost::asio::io_context& io_;
    PipelineFactory factory_;
    std::string kind_;
    std::function<long long()> now_;
    std::map<std::string, std::shared_ptr<Source>> sources_;
    std::shared_ptr<Source> open(const AppConfig& app);
    void receive(const std::shared_ptr<Source>& source);
    void fan_out(Source& source, std::string_view packet);
    void teardown(const std::shared_ptr<Source>& source, bool notify);
};
}
