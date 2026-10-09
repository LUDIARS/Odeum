#pragma once
#include "core/event_queue.hpp"
#include "../core/flv_feed.hpp"
#include "../core/media_codecs.hpp"
#include "../core/program_settings.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace odeum::program {
struct PublishStats {
    std::uint64_t bytes_sent{};
    std::uint64_t dropped_frames{};
    double bitrate_kbps{}; // over the last second
};

// One publish attempt to YouTube Live on a worker thread: TLS to the ingest, the RTMP publish
// (RtmpPublisher), then the FLV tags of the programme. Encoded frames are accepted from any
// thread; until the publish starts they are dropped, and after it starts the stream opens with
// the metadata and sequence headers and waits for a keyframe (keyframe_needed asks the encoder
// for one). When the queue falls more than 2 s behind, video is dropped up to the next keyframe.
// A failure ends the attempt; PublishSupervisor on the UI thread decides when to start again.
class YouTubePublisher {
public:
    struct Callbacks {
        std::function<void()> live;                       // UI thread
        std::function<void(const std::string& code)> failed; // UI thread; code is a fixed word
        std::function<void()> keyframe_needed;            // any thread
    };
    YouTubePublisher(presenter::EventQueue& ui, StreamMetadata metadata, Callbacks callbacks);
    ~YouTubePublisher();
    YouTubePublisher(const YouTubePublisher&) = delete;
    YouTubePublisher& operator=(const YouTubePublisher&) = delete;
    // Replaces any running attempt. The key is kept only for this attempt's commands.
    void start(const RtmpEndpoint& endpoint, std::string stream_key);
    void stop();
    void video(const presenter::EncodedFrame& frame);
    void audio(const AacPacket& packet);
    PublishStats stats() const;
private:
    void run(RtmpEndpoint endpoint, std::string key, std::uint64_t generation);
    void enqueue(std::vector<FlvTag> tags, bool keyframe, bool video);
    presenter::EventQueue& ui_;
    Callbacks callbacks_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    FlvFeed feed_;
    std::deque<FlvTag> queue_;
    std::size_t queued_bytes_ = 0;
    bool publishing_ = false, skip_to_keyframe_ = false;
    std::uint64_t bytes_sent_ = 0, dropped_ = 0, window_bytes_ = 0;
    double bitrate_kbps_ = 0;
    std::chrono::steady_clock::time_point window_start_;
    std::shared_ptr<std::atomic<std::uint64_t>> generation_;
    std::thread worker_;
    std::size_t max_queued_bytes_;
};
}
