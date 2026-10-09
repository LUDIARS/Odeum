#include "youtube_publisher.hpp"
#include "tls_connection.hpp"
#include "../core/rtmp_publisher.hpp"
#include <random>

namespace odeum::program {
namespace {
using namespace std::chrono_literals;
constexpr auto connect_timeout = 5000ms;
constexpr auto publish_timeout = 15s;

std::array<std::byte, rtmp_random_size> handshake_random() {
    std::random_device device;
    std::array<std::byte, rtmp_random_size> random{};
    for (auto& b : random) b = static_cast<std::byte>(device() & 0xff);
    return random;
}

std::uint32_t epoch_ms() {
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() & 0xffffffffu);
}
}

YouTubePublisher::YouTubePublisher(presenter::EventQueue& ui, StreamMetadata metadata, Callbacks callbacks)
    : ui_(ui), callbacks_(std::move(callbacks)), feed_(metadata, aac_rate, aac_channels),
      generation_(std::make_shared<std::atomic<std::uint64_t>>(0)),
      // Two seconds of the programme at its configured bitrate (plus audio).
      max_queued_bytes_(static_cast<std::size_t>(metadata.video_kbps + metadata.audio_kbps) * 1000 / 8 * 2) {}

YouTubePublisher::~YouTubePublisher() { stop(); }

void YouTubePublisher::start(const RtmpEndpoint& endpoint, std::string stream_key) {
    stop();
    const auto generation = ++*generation_;
    worker_ = std::thread([this, endpoint, key = std::move(stream_key), generation]() mutable { run(endpoint, std::move(key), generation); });
}

void YouTubePublisher::stop() {
    ++*generation_;
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(mutex_);
    publishing_ = false;
    queue_.clear();
    queued_bytes_ = 0;
    bitrate_kbps_ = 0;
}

void YouTubePublisher::enqueue(std::vector<FlvTag> tags, bool keyframe, bool video) {
    if (tags.empty()) return;
    std::lock_guard lock(mutex_);
    if (!publishing_) return;
    if (skip_to_keyframe_) {
        if (!video || !keyframe) { ++dropped_; return; }
        skip_to_keyframe_ = false;
    }
    if (queued_bytes_ > max_queued_bytes_) {
        // The ingest is not keeping up: drop what is queued and resume at the next keyframe.
        dropped_ += queue_.size();
        queue_.clear();
        queued_bytes_ = 0;
        skip_to_keyframe_ = true;
        if (callbacks_.keyframe_needed) callbacks_.keyframe_needed();
        return;
    }
    for (auto& tag : tags) {
        queued_bytes_ += tag.body.size();
        queue_.push_back(std::move(tag));
    }
    wake_.notify_one();
}

void YouTubePublisher::video(const presenter::EncodedFrame& frame) {
    std::vector<FlvTag> tags;
    {
        std::lock_guard lock(mutex_);
        if (!publishing_) return;
        tags = feed_.video(frame);
    }
    enqueue(std::move(tags), frame.keyframe, true);
}

void YouTubePublisher::audio(const AacPacket& packet) {
    std::vector<FlvTag> tags;
    {
        std::lock_guard lock(mutex_);
        if (!publishing_) return;
        tags = feed_.audio(packet.data, packet.timestamp_us);
    }
    enqueue(std::move(tags), false, false);
}

PublishStats YouTubePublisher::stats() const {
    std::lock_guard lock(mutex_);
    return {bytes_sent_, dropped_, bitrate_kbps_};
}

void YouTubePublisher::run(RtmpEndpoint endpoint, std::string key, std::uint64_t generation) {
    auto current = generation_;
    const auto alive = [&] { return current->load() == generation; };
    auto post = [this, current, generation](std::function<void()> work) {
        ui_.post([current, generation, work = std::move(work)] { if (current->load() == generation) work(); });
    };
    std::string code;
    try {
        TlsConnection tls(endpoint.host, endpoint.port, connect_timeout);
        RtmpPublisher rtmp({endpoint.tc_url, endpoint.app, std::move(key)}, epoch_ms(), handshake_random());
        tls.write(rtmp.start());
        std::vector<std::byte> buffer(64 * 1024);
        const auto deadline = std::chrono::steady_clock::now() + publish_timeout;
        while (!rtmp.publishing()) {
            if (!alive()) return;
            if (std::chrono::steady_clock::now() > deadline) throw RtmpError("timeout", "The ingest did not start the publish in time");
            const auto n = tls.read(buffer, 100ms);
            if (n > 0) if (auto reply = rtmp.receive(std::span(buffer).first(n)); !reply.empty()) tls.write(reply);
        }
        {
            std::lock_guard lock(mutex_);
            publishing_ = true;
            skip_to_keyframe_ = false;
            feed_.restart();
            queue_.clear();
            queued_bytes_ = 0;
            window_bytes_ = 0;
            window_start_ = std::chrono::steady_clock::now();
        }
        if (callbacks_.keyframe_needed) callbacks_.keyframe_needed();
        post([this] { if (callbacks_.live) callbacks_.live(); });
        std::vector<std::byte> out;
        while (alive()) {
            std::deque<FlvTag> batch;
            {
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock, 20ms, [&] { return !queue_.empty() || !alive(); });
                batch.swap(queue_);
                queued_bytes_ = 0;
            }
            out.clear();
            for (const auto& tag : batch) rtmp.send(tag, out);
            if (!out.empty()) tls.write(out);
            // Server control traffic (acknowledgement window, pings) without blocking the media.
            if (const auto n = tls.read(buffer, 0ms); n > 0)
                if (auto reply = rtmp.receive(std::span(buffer).first(n)); !reply.empty()) tls.write(reply);
            std::lock_guard lock(mutex_);
            bytes_sent_ += out.size();
            window_bytes_ += out.size();
            const auto now = std::chrono::steady_clock::now();
            if (now - window_start_ >= 1s) {
                bitrate_kbps_ = static_cast<double>(window_bytes_) * 8 / 1000 / std::chrono::duration<double>(now - window_start_).count();
                window_bytes_ = 0;
                window_start_ = now;
            }
        }
        return;
    } catch (const RtmpError& error) {
        code = error.code;
    } catch (const TlsError&) {
        code = "network";
    } catch (const std::exception&) {
        code = "network";
    }
    {
        std::lock_guard lock(mutex_);
        publishing_ = false;
        queue_.clear();
        queued_bytes_ = 0;
        bitrate_kbps_ = 0;
    }
    post([this, code] { if (callbacks_.failed) callbacks_.failed(code); });
}
}
