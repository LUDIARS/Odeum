#include "archive_recorder.hpp"
#include <odeum/message.hpp>
#include <chrono>
#include <fstream>
#include <random>

namespace odeum::program {
namespace {
constexpr std::size_t max_queue = 32 * 1024 * 1024;
void write(std::ofstream& file, std::span<const std::byte> bytes) {
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("archive_write_failed");
}
}
ArchiveRecorder::ArchiveRecorder(StreamMetadata metadata) : feed_(metadata, metadata.audio_rate, metadata.audio_channels) {}
ArchiveRecorder::~ArchiveRecorder() { stop(); }
void ArchiveRecorder::start(const std::filesystem::path& root, const std::string& project) {
    stop();
    if (project.empty() || project.size() > 80 || project.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos)
        throw std::invalid_argument("A GLab project ID is required for recording");
    const auto parent = root / project;
    std::filesystem::create_directories(parent);
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto directory = parent / (std::to_string(stamp) + "-" + std::to_string(std::random_device{}()));
    if (!std::filesystem::create_directory(directory)) throw std::runtime_error("archive_directory_exists");
    {
        std::lock_guard lock(mutex_);
        feed_.restart(); queue_.clear(); queued_bytes_ = 0;
        status_ = {true, {}, directory / "program.flv"};
    }
    try { worker_ = std::thread([this, directory, project] { run(directory, project); }); }
    catch (...) { std::lock_guard lock(mutex_); status_.recording = false; status_.error = "archive_worker_failed"; throw; }
}
void ArchiveRecorder::stop() {
    { std::lock_guard lock(mutex_); status_.recording = false; }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}
RecordingStatus ArchiveRecorder::status() const { std::lock_guard lock(mutex_); return status_; }
void ArchiveRecorder::enqueue(std::vector<FlvTag> tags) {
    for (auto& tag : tags) {
        if (queued_bytes_ + tag.body.size() > max_queue) {
            status_.recording = false; status_.error = "archive_disk_too_slow";
            wake_.notify_one(); return;
        }
        queued_bytes_ += tag.body.size(); queue_.push_back(std::move(tag));
    }
    wake_.notify_one();
}
void ArchiveRecorder::video(const presenter::EncodedFrame& frame) {
    std::lock_guard lock(mutex_);
    if (status_.recording) enqueue(feed_.video(frame));
}
void ArchiveRecorder::audio(const AacPacket& packet) {
    std::lock_guard lock(mutex_);
    if (status_.recording) enqueue(feed_.audio(packet.data, packet.timestamp_us));
}
void ArchiveRecorder::run(std::filesystem::path directory, std::string project) {
    try {
        std::ofstream file(directory / "program.flv.partial", std::ios::binary);
        write(file, flv_header());
        std::uint64_t tags_written = 0;
        std::uint32_t duration = 0;
        for (;;) {
            std::deque<FlvTag> batch;
            bool done;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return !status_.recording || !queue_.empty(); });
                batch.swap(queue_); queued_bytes_ = 0; done = !status_.recording;
                if (!status_.error.empty()) throw std::runtime_error(status_.error);
            }
            for (const auto& tag : batch) {
                write(file, flv_serialize(tag));
                duration = std::max(duration, tag.timestamp_ms); ++tags_written;
            }
            file.flush();
            if (!file) throw std::runtime_error("archive_flush_failed");
            if (done) break;
        }
        file.close();
        if (!file) throw std::runtime_error("archive_close_failed");
        if (!tags_written) throw std::runtime_error("archive_no_keyframe");
        std::filesystem::rename(directory / "program.flv.partial", directory / "program.flv");
        const Json manifest{{"version", 1}, {"projectId", project}, {"status", "ready"},
            {"file", "program.flv"}, {"durationMs", duration}, {"bytes", std::filesystem::file_size(directory / "program.flv")}};
        std::ofstream index(directory / "archive.json.partial", std::ios::binary);
        index << manifest.dump(2) << '\n'; index.close();
        if (!index) throw std::runtime_error("archive_manifest_failed");
        std::filesystem::rename(directory / "archive.json.partial", directory / "archive.json");
    } catch (const std::exception&) {
        std::lock_guard lock(mutex_);
        status_.recording = false;
        if (status_.error.empty()) status_.error = "archive_write_failed";
        queue_.clear(); queued_bytes_ = 0;
    }
}
}
