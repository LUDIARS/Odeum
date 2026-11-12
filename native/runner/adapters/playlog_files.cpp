#include "playlog_files.hpp"
#include "../core/app_config.hpp"
#include "../core/playlog_row.hpp"
#include "clock.hpp"
#include "paths.hpp"
#include <lorica/input_budget.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace odeum::runner {
namespace fs = std::filesystem;

namespace {
#ifdef _WIN32
std::wstring wide(const std::string& path) { return to_path(path).wstring(); }
#endif

// Exclusive create + append-only writes; the descriptor is closed on close() or destruction.
class FileWriter final : public PlayLogWriter {
public:
    explicit FileWriter(const std::string& path) {
#ifdef _WIN32
        handle_ = CreateFileW(wide(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("EEXIST or unwritable: " + path);
#else
        fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd_ < 0) throw std::runtime_error("cannot create play log (exists or unwritable)");
#endif
    }
    ~FileWriter() override { close(); }
    void write(const Json& row) override {
        const auto line = dump(row) + "\n";
#ifdef _WIN32
        if (handle_ == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        if (!WriteFile(handle_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) || written != line.size()) throw std::runtime_error("play log write failed");
#else
        if (fd_ < 0) return;
        std::size_t offset = 0;
        while (offset < line.size()) {
            const auto n = ::write(fd_, line.data() + offset, line.size() - offset);
            if (n <= 0) throw std::runtime_error("play log write failed");
            offset += static_cast<std::size_t>(n);
        }
#endif
    }
    void close() override {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
#else
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

std::string iso_of(const fs::file_time_type& time) {
    const auto system = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::clock_cast<std::chrono::system_clock>(time));
    return iso_time(system.time_since_epoch().count());
}
}

std::unique_ptr<PlayLogWriter> open_play_log_file(const std::string& path) {
    fs::create_directories(to_path(path).parent_path());
    return std::make_unique<FileWriter>(path);
}

Json to_json(const PlayLogEntry& e) {
    return Json{{"appId", e.app_id}, {"run", e.run}, {"bytes", e.bytes}, {"updatedAt", e.updated_at}};
}

std::vector<PlayLogEntry> list_play_logs(const std::string& dir, const std::optional<std::string>& app_id) {
    std::vector<PlayLogEntry> entries;
    std::error_code error;
    const auto root = to_path(dir);
    if (!fs::is_directory(root, error)) return entries;
    std::vector<std::string> apps;
    if (app_id) {
        if (is_valid_app_id(*app_id)) apps.push_back(*app_id);
    } else {
        for (const auto& item : fs::directory_iterator(root, error)) {
            const auto text = from_path(item.path().filename());
            if (item.is_directory(error) && !item.is_symlink(error) && is_valid_app_id(text)) apps.push_back(text);
        }
    }
    for (const auto& app : apps) {
        const auto app_dir = root / to_path(app);
        if (!fs::is_directory(app_dir, error)) continue;
        for (const auto& item : fs::directory_iterator(app_dir, error)) {
            const auto name = from_path(item.path().filename());
            if (name.size() <= 6 || name.compare(name.size() - 6, 6, ".jsonl") != 0) continue;
            const auto run = name.substr(0, name.size() - 6);
            if (!is_valid_run_id(run) || !item.is_regular_file(error)) continue;
            entries.push_back({app, run, item.file_size(error), iso_of(item.last_write_time(error))});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const PlayLogEntry& a, const PlayLogEntry& b) { return a.updated_at > b.updated_at; });
    return entries;
}

std::optional<std::string> read_file_limited(const std::string& path, std::size_t limit) {
    std::error_code error;
    const auto p = to_path(path);
    if (!fs::is_regular_file(p, error)) return std::nullopt;
    const auto size = fs::file_size(p, error);
    if (error) return std::nullopt;
    lorica::input_budget budget({limit, 1, 1, 1});
    if (size > limit || !budget.consume(static_cast<std::size_t>(size), 1, 1)) return std::nullopt;
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::string data(static_cast<std::size_t>(size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    data.resize(static_cast<std::size_t>(in.gcount()));
    return data;
}

std::optional<std::string> read_play_log(const std::string& dir, const std::string& app_id, const std::string& run) {
    if (!is_valid_app_id(app_id) || !is_valid_run_id(run)) return std::nullopt;
    return read_file_limited(from_path(to_path(dir) / to_path(app_id) / to_path(run + ".jsonl")), max_play_log_bytes);
}

std::string resolve_player_salt(const std::string& dir, const std::optional<std::string>& env_salt) {
    auto trim = [](std::string text) {
        const auto begin = text.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return std::string();
        return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
    };
    if (env_salt && !trim(*env_salt).empty()) return *env_salt;
    const auto file = from_path(to_path(dir) / ".salt");
    if (auto saved = read_file_limited(file, 4096)) {
        const auto value = trim(*saved);
        if (!value.empty()) return value;
    }
    fs::create_directories(to_path(dir));
    const auto salt = random_hex(32);
#ifdef _WIN32
    HANDLE h = CreateFileW(wide(file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot write play log salt");
    DWORD written = 0;
    WriteFile(h, salt.data(), static_cast<DWORD>(salt.size()), &written, nullptr);
    CloseHandle(h);
#else
    const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error("cannot write play log salt");
    const auto n = ::write(fd, salt.data(), salt.size());
    ::close(fd);
    if (n != static_cast<ssize_t>(salt.size())) throw std::runtime_error("cannot write play log salt");
#endif
    return salt;
}
}
