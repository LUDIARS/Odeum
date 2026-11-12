#pragma once
// Periodic screenshots pushed to subscribed WebSocket clients (custos/src/capture/screenshot-stream.ts).
// One timer per app, reference-counted by session, one capture in flight per app.
#include "../adapters/media_tools.hpp"
#include "../core/stream_prefs.hpp"
#include <boost/asio/steady_timer.hpp>
#include <map>
#include <set>

namespace odeum::runner {
class ScreenshotSource {
public:
    virtual ~ScreenshotSource() = default;
    // In-app bridge /screenshot when configured, else the ffmpeg capture.
    virtual void capture(const AppConfig& app, std::function<void(PngResult)> done) = 0;
    virtual void scale(std::string png, std::optional<long long> max_width, std::function<void(PngResult)> done) = 0;
};

// The in-app bridge /screenshot when configured, else ffmpeg (MediaTools).
std::unique_ptr<ScreenshotSource> make_screenshot_source(boost::asio::io_context& io, MediaTools& media);

class ScreenshotStreamer {
public:
    using FrameListener = std::function<void(const std::string& app_id, long long ts, const std::string& png)>;
    ScreenshotStreamer(boost::asio::io_context& io, ScreenshotSource& source, RuntimeStreamPrefs& prefs,
        std::optional<double> env_interval_sec, std::function<long long()> now_ms);
    bool acquire(const AppConfig& app, const std::string& key);
    void release(const std::string& app_id, const std::string& key);
    void release_all(const std::string& key);
    void stop(const std::string& app_id);
    void shutdown();
    void on_frame(FrameListener listener) { listeners_.push_back(std::move(listener)); }
    std::size_t active_streams() const { return streams_.size(); }
    double effective_interval_sec(const AppConfig& app) const;
private:
    struct Stream {
        std::unique_ptr<boost::asio::steady_timer> timer;
        std::set<std::string> refs;
        bool inflight = false;
        AppConfig config;
        std::chrono::milliseconds period{1000};
        std::uint64_t generation = 0;
    };
    boost::asio::io_context& io_;
    ScreenshotSource& source_;
    RuntimeStreamPrefs& prefs_;
    std::optional<double> env_interval_;
    std::function<long long()> now_;
    std::map<std::string, Stream> streams_;
    std::vector<FrameListener> listeners_;
    std::uint64_t next_generation_ = 0;
    void schedule(const std::string& app_id, std::uint64_t generation);
    void tick(const std::string& app_id);
    void restart_all();
};
}
