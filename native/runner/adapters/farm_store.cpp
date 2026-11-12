#include "farm_store.hpp"
#include "paths.hpp"
#include "playlog_files.hpp"
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace odeum::runner {
namespace fs = std::filesystem;

namespace {
bool absolute(const std::string& path) {
    return to_path(path).is_absolute();
}

#ifdef _WIN32
struct Exclusive {
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool exists = false;
    explicit Exclusive(const std::string& path) {
        handle = CreateFileW(to_path(path).wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        exists = handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS;
    }
    bool ok() const { return handle != INVALID_HANDLE_VALUE; }
    bool write_all(const std::string& data) {
        DWORD written = 0;
        return WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() && FlushFileBuffers(handle);
    }
    void close() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); handle = INVALID_HANDLE_VALUE; }
    ~Exclusive() { close(); }
};
bool replace_file(const std::string& from, const std::string& to) {
    return MoveFileExW(to_path(from).wstring().c_str(), to_path(to).wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
void remove_file(const std::string& path) { DeleteFileW(to_path(path).wstring().c_str()); }
#else
struct Exclusive {
    int fd = -1;
    bool exists = false;
    explicit Exclusive(const std::string& path) {
        fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        exists = fd < 0 && errno == EEXIST;
    }
    bool ok() const { return fd >= 0; }
    bool write_all(const std::string& data) {
        std::size_t offset = 0;
        while (offset < data.size()) {
            const auto n = ::write(fd, data.data() + offset, data.size() - offset);
            if (n <= 0) return false;
            offset += static_cast<std::size_t>(n);
        }
        return ::fsync(fd) == 0;
    }
    void close() { if (fd >= 0) ::close(fd); fd = -1; }
    ~Exclusive() { close(); }
};
bool replace_file(const std::string& from, const std::string& to) { return std::rename(from.c_str(), to.c_str()) == 0; }
void remove_file(const std::string& path) { ::unlink(path.c_str()); }
#endif
}

FileFarmStore::FileFarmStore(const std::string& path) {
    if (!absolute(path)) throw FarmContractError("CUSTOS_FARM_STATE_FILE must be an absolute local file path");
    // Directory junctions / symlinks must not create independent locks for the same file.
    path_ = from_path(fs::canonical(to_path(path)));
}

void FileFarmStore::transaction(const std::function<void(FarmState&)>& action) {
    const auto lock_path = path_ + ".lock";
    Exclusive lock(lock_path);
    if (!lock.ok()) {
        if (lock.exists) throw FarmError("farm_store_locked", 503);
        throw FarmContractError("farm lock unavailable");
    }
    const auto temp_path = path_ + ".pending";
    bool owns_temp = false;
    auto cleanup = [&] {
        if (owns_temp) remove_file(temp_path);
        lock.close();
        remove_file(lock_path);
    };
    try {
        // The operator provisions the state once; a lost file must never silently drop reservations.
        const auto text = read_file_limited(path_, state_file_limits.bytes);
        if (!text) throw FarmContractError("farm state unreadable");
        auto state = parse_farm_state(parse_json(*text, state_file_limits));
        const auto before = dump(to_json(state));
        action(state);
        const auto after = dump(to_json(state));
        if (after != before) {
            Exclusive temp(temp_path);  // refuses unexplained residue from a crash
            if (!temp.ok()) throw FarmContractError("farm state temp file unavailable");
            owns_temp = true;
            if (!temp.write_all(after)) throw FarmContractError("farm state write failed");
            temp.close();
            if (!replace_file(temp_path, path_)) throw FarmContractError("farm state rename failed");
            owns_temp = false;
        }
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
}
}
