#pragma once
#include "flv_feed.hpp"
#include "media_codecs.hpp"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace odeum::program {
struct RecordingStatus {
    bool recording = false;
    std::string error;
    std::filesystem::path file;
};
// A bounded, lossless recording queue. Disk failure/overload ends this recording explicitly;
// the live encoder never waits for disk I/O and no incomplete file is advertised as an archive.
class ArchiveRecorder {
public:
    explicit ArchiveRecorder(StreamMetadata metadata);
    ~ArchiveRecorder();
    void start(const std::filesystem::path& root, const std::string& project);
    void stop();
    void video(const presenter::EncodedFrame& frame);
    void audio(const AacPacket& packet);
    RecordingStatus status() const;
private:
    void enqueue(std::vector<FlvTag> tags); // mutex held
    void run(std::filesystem::path directory, std::string project);
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    FlvFeed feed_;
    std::deque<FlvTag> queue_;
    std::size_t queued_bytes_ = 0;
    RecordingStatus status_;
    std::thread worker_;
};
}
