#include "check.hpp"
#include "core/archive_recorder.hpp"
#include "core/archive_reader.hpp"
#include "core/capture_region.hpp"
#include <chrono>
#include <filesystem>

using namespace odeum::program;
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("odeum-archive-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto crop = odeum::presenter::capture_pixels({25,25,50,50}, 1920, 1080);
        check(crop.x == 480 && crop.y == 270 && crop.width == 960 && crop.height == 540, "Region matches selected display");
        bool rejected = false;
        try { odeum::presenter::capture_pixels({90,0,50,50},1920,1080); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Out-of-display region rejected");
        ArchiveRecorder recorder({1920,1080,30,8000,128,48000,2});
        rejected = false;
        try { recorder.start(root, "../outside"); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Project cannot escape archive root");
        recorder.start(root, "project-one");
        check(project_archives(root, "project-one").empty(), "Recording is not yet an archive");
        std::vector<std::byte> key;
        for (int b : {0,0,0,1,0x67,0x64,0,0x28,0xAC,0,0,0,1,0x68,0xEE,0x3C,0x80,0,0,0,1,0x65,0x88}) key.push_back(static_cast<std::byte>(b));
        recorder.video({key,1000000,1000000,true});
        recorder.audio({{std::byte{0x21}},1020000});
        recorder.stop();
        check(recorder.status().error.empty(), "Recording finalizes successfully");
        const auto files = project_archives(root,"project-one");
        check(files.size() == 1 && project_archives(root,"project-two").empty(), "Archive catalog is project-scoped");
        {
            ArchiveReader reader(files.front());
            int count = 0;
            while (const auto tag = reader.next()) { ++count; check(tag->timestamp_ms <= 20, "Recording uses a relative timeline"); }
            check(count == 5, "Headers, IDR and audio survive round trip");
        }
        recorder.start(root,"project-one"); recorder.stop();
        check(!recorder.status().error.empty(), "No keyframe cannot become a ready archive");
        check(project_archives(root,"project-one").size() == 1, "Incomplete recording stays out of catalog");
        std::filesystem::resize_file(files.front(), 14);
        rejected = false;
        try { ArchiveReader reader(files.front()); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Truncated finalized recording is rejected");
        std::filesystem::remove_all(root);
        return 0;
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
}
