#include "rtp_source_pool.hpp"
#include "../adapters/log.hpp"
#include <boost/asio/post.hpp>
#include <array>

namespace odeum::runner {
namespace asio = boost::asio;
namespace {
constexpr std::size_t stderr_tail_max = 20;
constexpr std::size_t datagram_buffer = 2048;  // ffmpeg pkt_size=1200; a full buffer means a truncated datagram
}

struct SharedRtpSourcePool::Source {
    std::string app_id;
    std::optional<VideoFeedbackPolicy> policy;
    std::unique_ptr<asio::ip::udp::socket> udp;
    std::unique_ptr<RtpPipeline> pipeline;
    int rtp_port = 0;
    std::map<std::string, RtpSink> sinks;
    std::deque<std::string> stderr_tail;
    std::vector<std::pair<std::string, std::function<void(std::variant<int, std::string>)>>> waiting;
    std::array<char, datagram_buffer> buffer{};
    bool ready = false, closed = false;
};

SharedRtpSourcePool::SharedRtpSourcePool(asio::io_context& io, PipelineFactory factory, std::string kind, std::function<long long()> now_ms)
    : io_(io), factory_(std::move(factory)), kind_(std::move(kind)), now_(std::move(now_ms)) {}

SharedRtpSourcePool::~SharedRtpSourcePool() { shutdown(); }

void SharedRtpSourcePool::acquire(const AppConfig& app, const std::string& subscriber, RtpSink sink, std::function<void(std::variant<int, std::string>)> done) {
    auto it = sources_.find(app.id);
    std::shared_ptr<Source> source = it != sources_.end() ? it->second : nullptr;
    if (!source) {
        try {
            source = open(app);
        } catch (const std::exception& error) {
            return asio::post(io_, [done, message = std::string(error.what())] { done(message); });
        }
    }
    source->sinks[subscriber] = std::move(sink);
    if (source->ready) {
        // A late joiner cannot decode until the next key frame (long H.264 GOPs).
        if (source->pipeline->takes_feedback()) source->pipeline->request_keyframe();
        return asio::post(io_, [done, port = source->rtp_port] { done(port); });
    }
    source->waiting.emplace_back(subscriber, std::move(done));
}

std::shared_ptr<SharedRtpSourcePool::Source> SharedRtpSourcePool::open(const AppConfig& app) {
    auto source = std::make_shared<Source>();
    source->app_id = app.id;
    if (app.in_app_bridge && kind_ == "video") {
        const auto& v = app.in_app_bridge->video;
        source->policy.emplace(BitrateLimits{v.bitrate, v.min_bitrate, v.max_bitrate});
    }
    source->udp = std::make_unique<asio::ip::udp::socket>(io_, asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    source->rtp_port = source->udp->local_endpoint().port();
    source->pipeline = factory_();
    sources_[app.id] = source;
    receive(source);
    std::weak_ptr<Source> weak = source;
    RtpPipeline::Events events;
    events.packet = [this, weak](std::string_view packet) { if (auto s = weak.lock(); s && !s->closed) fan_out(*s, packet); };
    events.exit = [this, weak] { if (auto s = weak.lock()) teardown(s, true); };
    events.stderr_line = [weak](const std::string& line) {
        if (auto s = weak.lock()) {
            s->stderr_tail.push_back(line);
            while (s->stderr_tail.size() > stderr_tail_max) s->stderr_tail.pop_front();
        }
    };
    source->pipeline->start(app, source->rtp_port, std::move(events), [this, weak, kind = kind_, app_id = app.id](std::optional<StartError> error) {
        auto s = weak.lock();
        if (!s) return;
        auto waiting = std::move(s->waiting);
        s->waiting.clear();
        if (error || s->closed) {
            const auto message = error ? error->message : kind + " source for " + app_id + " ended while starting";
            for (auto& [subscriber, done] : waiting) s->sinks.erase(subscriber);
            teardown(s, false);
            for (auto& [subscriber, done] : waiting) done(message);
            return;
        }
        s->ready = true;
        log::info("rtp-source", "source opened", {{"appId", app_id}, {"kind", kind}, {"rtpPort", s->rtp_port}});
        // Released while starting: stop now that the start has settled (nothing is orphaned).
        if (s->sinks.empty()) {
            teardown(s, false);
            for (auto& [subscriber, done] : waiting) done(kind + " source for " + app_id + " ended while starting");
            return;
        }
        if (s->pipeline->takes_feedback()) s->pipeline->request_keyframe();
        for (auto& [subscriber, done] : waiting) {
            if (s->sinks.contains(subscriber)) done(s->rtp_port);
            else done(kind + " source for " + app_id + " ended while starting");
        }
    });
    return source;
}

void SharedRtpSourcePool::receive(const std::shared_ptr<Source>& source) {
    std::weak_ptr<Source> weak = source;
    source->udp->async_receive(asio::buffer(source->buffer), [this, weak](const boost::system::error_code& error, std::size_t n) {
        auto s = weak.lock();
        if (!s || s->closed || error == asio::error::operation_aborted) return;
        if (!error && n > 0 && n < datagram_buffer) fan_out(*s, std::string_view(s->buffer.data(), n));
        receive(s);
    });
}

void SharedRtpSourcePool::fan_out(Source& source, std::string_view packet) {
    for (auto& [subscriber, sink] : source.sinks) {
        try {
            if (sink.packet) sink.packet(packet);
        } catch (const std::exception& error) {
            log::warn("rtp-source", "RTP sink failed", {{"appId", source.app_id}, {"subscriberId", subscriber}, {"err", error.what()}});
        }
    }
}

void SharedRtpSourcePool::picture_loss(const std::string& app_id, const std::string& subscriber) {
    auto it = sources_.find(app_id);
    if (it == sources_.end() || it->second->closed || !it->second->policy || !it->second->sinks.contains(subscriber)) return;
    auto& s = *it->second;
    if (s.pipeline->takes_feedback() && s.policy->should_request_keyframe(now_())) s.pipeline->request_keyframe();
}

void SharedRtpSourcePool::estimate(const std::string& app_id, const std::string& subscriber, double bps) {
    auto it = sources_.find(app_id);
    if (it == sources_.end() || it->second->closed || !it->second->policy || !it->second->sinks.contains(subscriber)) return;
    auto& s = *it->second;
    if (!s.pipeline->takes_feedback()) return;
    if (const auto next = s.policy->update_estimate(subscriber, bps, now_())) s.pipeline->set_bitrate(*next);
}

void SharedRtpSourcePool::release(const std::string& app_id, const std::string& subscriber) {
    auto it = sources_.find(app_id);
    if (it == sources_.end()) return;
    auto source = it->second;
    if (source->sinks.erase(subscriber) == 0) return;
    if (!source->sinks.empty()) {
        // The slowest viewer left: the others may get a higher bitrate.
        if (source->policy && source->pipeline->takes_feedback()) {
            if (const auto next = source->policy->remove_session(subscriber, now_())) source->pipeline->set_bitrate(*next);
        }
        return;
    }
    if (source->ready) teardown(source, false);
    // Still starting: the start callback stops it once settled.
}

void SharedRtpSourcePool::teardown(const std::shared_ptr<Source>& source, bool notify) {
    if (source->closed) return;
    source->closed = true;
    if (auto it = sources_.find(source->app_id); it != sources_.end() && it->second == source) sources_.erase(it);
    try { source->pipeline->stop(); } catch (const std::exception&) { /* already ended; best effort */ }
    boost::system::error_code ignored;
    source->udp->close(ignored);
    auto sinks = std::move(source->sinks);
    source->sinks.clear();
    log::info("rtp-source", "source closed", {{"appId", source->app_id}, {"kind", kind_}, {"subscribers", sinks.size()}});
    if (notify) for (auto& [id, sink] : sinks) if (sink.end) sink.end();
}

std::vector<std::string> SharedRtpSourcePool::stderr_tail(const std::string& app_id) const {
    auto it = sources_.find(app_id);
    if (it == sources_.end()) return {};
    return {it->second->stderr_tail.begin(), it->second->stderr_tail.end()};
}

std::size_t SharedRtpSourcePool::subscribers(const std::string& app_id) const {
    auto it = sources_.find(app_id);
    return it == sources_.end() ? 0 : it->second->sinks.size();
}

void SharedRtpSourcePool::shutdown() {
    auto all = sources_;
    for (auto& [id, source] : all) teardown(source, true);
}
}
