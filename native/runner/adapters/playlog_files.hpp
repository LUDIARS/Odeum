#pragma once
// Play-log storage (custos/src/playlog/playlog-writer.ts, playlog-store.ts, player-hash.ts salt):
// one JSONL file per run under <dir>/<appId>/<run>.jsonl, never reopened; listing and reading only
// accept validated ids, so nothing outside <dir> is reachable.
#include "../core/json.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace odeum::runner {
class PlayLogWriter {
public:
    virtual ~PlayLogWriter() = default;
    virtual void write(const Json& row) = 0;
    virtual void close() = 0;
};
// Creates parent directories; fails when the file already exists (exclusive create).
std::unique_ptr<PlayLogWriter> open_play_log_file(const std::string& path);

struct PlayLogEntry {
    std::string app_id, run;
    std::uintmax_t bytes = 0;
    std::string updated_at;  // ISO-8601
};
Json to_json(const PlayLogEntry& entry);
// Newest first. With app_id: that app only (an invalid id lists nothing).
std::vector<PlayLogEntry> list_play_logs(const std::string& dir, const std::optional<std::string>& app_id);
inline constexpr std::size_t max_play_log_bytes = 64u * 1024 * 1024;
// nullopt for invalid ids, a missing file or a file over max_play_log_bytes.
std::optional<std::string> read_play_log(const std::string& dir, const std::string& app_id, const std::string& run);
// CUSTOS_PLAYLOG_SALT, else <dir>/.salt, else 32 random bytes (hex) written there with mode 0600.
std::string resolve_player_salt(const std::string& dir, const std::optional<std::string>& env_salt);
// Reads a whole regular file up to `limit` bytes (charged before reading); nullopt otherwise.
std::optional<std::string> read_file_limited(const std::string& path, std::size_t limit);
}
