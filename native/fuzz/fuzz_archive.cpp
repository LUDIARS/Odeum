// libFuzzer entry: a recording directory's archive.json and program.flv (program/core
// archive_reader.cpp). The input is split at the first NUL: manifest before, FLV after. The
// manifest's "bytes" is rewritten to the FLV's size so the tag reader is reached.
#include "core/archive_reader.hpp"
#include <odeum/message.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {
const std::filesystem::path& directory() {
    static const auto path = [] {
        auto dir = std::filesystem::temp_directory_path() / ("odeum_fuzz_archive_" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
        return dir;
    }();
    return path;
}
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string input(reinterpret_cast<const char*>(data), size);
    const auto split = input.find('\0');
    std::string manifest = input.substr(0, split);
    const std::string flv = split == std::string::npos ? std::string() : input.substr(split + 1);
    try {
        auto json = odeum::Json::parse(manifest);
        if (json.is_object()) { json["bytes"] = flv.size(); manifest = json.dump(); }
    } catch (const odeum::Json::exception&) {
    }
    std::ofstream(directory() / "archive.json", std::ios::binary | std::ios::trunc) << manifest;
    std::ofstream(directory() / "program.flv", std::ios::binary | std::ios::trunc) << flv;
    try {
        odeum::program::ArchiveReader reader(directory() / "program.flv");
        for (int tags = 0; tags < 64 && reader.next(); ++tags) {}
    } catch (const std::exception&) {
        // Every refusal of a broken recording is an exception (invalid_argument, runtime_error,
        // ProtocolError, a JSON error); a crash or a sanitizer report is the failure.
    }
    return 0;
}
