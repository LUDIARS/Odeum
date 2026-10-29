#pragma once
// Locked, atomic JSON transactions on the farm state file (custos/src/farm/store.ts): an exclusive
// lock file, no cached state, a crash-refusing exclusive temp file, fsync, then rename.
#include "../core/farm.hpp"

namespace odeum::runner {
class FileFarmStore final : public FarmStore {
public:
    // `path` must be absolute and exist; symlinks are resolved so one file has one lock.
    explicit FileFarmStore(const std::string& path);
    void transaction(const std::function<void(FarmState&)>& action) override;
    const std::string& path() const { return path_; }
private:
    std::string path_;
};
}
