#include "screenshot_streamer.hpp"
#include "../adapters/bridge_client.hpp"
#include "../adapters/log.hpp"
#include <boost/asio/post.hpp>
#include <cmath>

namespace odeum::runner {
namespace {
class BridgeOrFfmpeg final : public ScreenshotSource {
public:
    BridgeOrFfmpeg(boost::asio::io_context& io, MediaTools& media) : io_(io), media_(media) {}
    void capture(const AppConfig& app, std::function<void(PngResult)> done) override {
        if (!app.in_app_bridge) return media_.capture_png(app, std::move(done));
        bridge_fetch_screenshot(io_, {app.in_app_bridge->host, app.in_app_bridge->port}, [done = std::move(done)](std::variant<std::string, BridgeCallError> result) {
            if (auto* png = std::get_if<std::string>(&result)) return done(PngResult(std::in_place_index<0>, std::move(*png)));
            done(PngResult(std::in_place_index<1>, std::get<BridgeCallError>(result).message));
        });
    }
    void scale(std::string png, std::optional<long long> max_width, std::function<void(PngResult)> done) override {
        media_.scale_png(std::move(png), max_width, std::move(done));
    }
private:
    boost::asio::io_context& io_;
    MediaTools& media_;
};

std::chrono::milliseconds period_of(double seconds) {
    return std::chrono::milliseconds(std::max<long long>(1, static_cast<long long>(std::llround(seconds * 1000))));
}
}

std::unique_ptr<ScreenshotSource> make_screenshot_source(boost::asio::io_context& io, MediaTools& media) {
    return std::make_unique<BridgeOrFfmpeg>(io, media);
}

ScreenshotStreamer::ScreenshotStreamer(boost::asio::io_context& io, ScreenshotSource& source, RuntimeStreamPrefs& prefs,
    std::optional<double> env_interval_sec, std::function<long long()> now_ms)
    : io_(io), source_(source), prefs_(prefs), env_interval_(env_interval_sec), now_(std::move(now_ms)) {
    // intervalSec changes re-arm every timer; maxWidth is read at each frame.
    prefs_.on_change([this](const StreamPrefs&) { restart_all(); });
}

double ScreenshotStreamer::effective_interval_sec(const AppConfig& app) const {
    if (const auto p = prefs_.get().interval_sec) return *p;
    if (env_interval_) return *env_interval_;
    return app.capture ? app.capture->interval_sec : 1;
}

bool ScreenshotStreamer::acquire(const AppConfig& app, const std::string& key) {
    const auto interval = effective_interval_sec(app);
    if (interval <= 0) {
        log::debug("screenshot-stream", "auto-stream disabled (intervalSec <= 0)", {{"appId", app.id}});
        return false;
    }
    if (!app.capture && !app.in_app_bridge) {
        log::debug("screenshot-stream", "no capture / inAppBridge config — auto-stream skipped", {{"appId", app.id}});
        return false;
    }
    if (auto it = streams_.find(app.id); it != streams_.end()) {
        it->second.refs.insert(key);
        return true;
    }
    Stream stream;
    stream.timer = std::make_unique<boost::asio::steady_timer>(io_);
    stream.refs.insert(key);
    stream.config = app;
    stream.period = period_of(interval);
    stream.generation = ++next_generation_;
    const auto generation = stream.generation;
    streams_.emplace(app.id, std::move(stream));
    log::info("screenshot-stream", "auto-stream started", {{"appId", app.id}, {"intervalSec", js_number(interval)}});
    schedule(app.id, generation);
    boost::asio::post(io_, [this, id = app.id, generation] {
        if (auto it = streams_.find(id); it != streams_.end() && it->second.generation == generation) tick(id);
    });
    return true;
}

void ScreenshotStreamer::schedule(const std::string& app_id, std::uint64_t generation) {
    auto it = streams_.find(app_id);
    if (it == streams_.end()) return;
    it->second.timer->expires_after(it->second.period);
    it->second.timer->async_wait([this, app_id, generation](const boost::system::error_code& error) {
        if (error) return;
        auto s = streams_.find(app_id);
        if (s == streams_.end() || s->second.generation != generation) return;
        schedule(app_id, generation);
        tick(app_id);
    });
}

void ScreenshotStreamer::release(const std::string& app_id, const std::string& key) {
    auto it = streams_.find(app_id);
    if (it == streams_.end()) return;
    it->second.refs.erase(key);
    if (it->second.refs.empty()) {
        it->second.timer->cancel();
        streams_.erase(it);
        log::info("screenshot-stream", "auto-stream stopped (no subscribers)", {{"appId", app_id}});
    }
}

void ScreenshotStreamer::release_all(const std::string& key) {
    std::vector<std::string> ids;
    for (const auto& [id, s] : streams_) ids.push_back(id);
    for (const auto& id : ids) release(id, key);
}

void ScreenshotStreamer::stop(const std::string& app_id) {
    auto it = streams_.find(app_id);
    if (it == streams_.end()) return;
    it->second.timer->cancel();
    streams_.erase(it);
    log::info("screenshot-stream", "auto-stream stopped (forced)", {{"appId", app_id}});
}

void ScreenshotStreamer::shutdown() {
    for (auto& [id, s] : streams_) s.timer->cancel();
    streams_.clear();
}

void ScreenshotStreamer::restart_all() {
    std::vector<std::string> ids;
    for (const auto& [id, s] : streams_) ids.push_back(id);
    for (const auto& id : ids) {
        auto& s = streams_.at(id);
        const auto interval = effective_interval_sec(s.config);
        s.timer->cancel();
        if (interval <= 0) {
            streams_.erase(id);
            log::info("screenshot-stream", "auto-stream stopped (intervalSec became 0)", {{"appId", id}});
            continue;
        }
        s.period = period_of(interval);
        s.generation = ++next_generation_;
        schedule(id, s.generation);
        log::info("screenshot-stream", "auto-stream interval updated", {{"appId", id}, {"intervalSec", js_number(interval)}});
    }
}

void ScreenshotStreamer::tick(const std::string& app_id) {
    auto it = streams_.find(app_id);
    if (it == streams_.end() || it->second.inflight) return;
    it->second.inflight = true;
    const auto generation = it->second.generation;
    source_.capture(it->second.config, [this, app_id, generation](PngResult captured) {
        auto finish = [this, app_id] {
            if (auto s = streams_.find(app_id); s != streams_.end()) s->second.inflight = false;
        };
        if (!png_ok(captured)) {
            log::debug("screenshot-stream", "tick capture failed (ignored — timer continues)", {{"appId", app_id}, {"err", std::get<1>(captured)}});
            return finish();
        }
        (void)generation;
        source_.scale(std::move(std::get<0>(captured)), prefs_.get().max_width, [this, app_id, finish](PngResult scaled) {
            if (png_ok(scaled)) {
                const auto ts = now_();
                for (const auto& listener : listeners_) listener(app_id, ts, std::get<0>(scaled));
            } else {
                log::debug("screenshot-stream", "tick capture failed (ignored — timer continues)", {{"appId", app_id}, {"err", std::get<1>(scaled)}});
            }
            finish();
        });
    });
}
}
