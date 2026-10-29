#pragma once
// Play-log rows (custos/src/playlog/playlog-row.ts, player-hash.ts). Columns are top-level keys that
// Commentarii's `guide import plays` mapping reads by name; game state is flattened to state.<key>.
// The player column is an HMAC of the user id, never the id itself.
#include "json.hpp"
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace odeum::runner {
struct KeyInput { std::string key; bool down; };
struct PointerInput { double x, y; std::string button, phase; };
struct ButtonInput { std::string id; };
using PlayInput = std::variant<KeyInput, PointerInput, ButtonInput>;

struct PlayLogRowBase {
    std::string run, player, app;
    long long seq = 0, t_ms = 0;
    std::string at;  // ISO-8601 UTC with milliseconds
};

// "start" | "input" | "state" | "comment" | "end"
Json build_row(const PlayLogRowBase& base, const std::string& kind, const Json& fields, const std::optional<Json>& state);
std::string input_name(const PlayInput& input);
Json input_fields(const PlayInput& input);
// YYYYMMDD-HHmmss-<6 of [a-z0-9]> in UTC; `pick(36)` returns an index in [0, 36).
std::string make_run_id(long long epoch_ms, const std::function<std::uint32_t(std::uint32_t)>& pick);
bool is_valid_run_id(std::string_view run);
// Date#toISOString
std::string iso_time(long long epoch_ms);
// hex(HMAC-SHA256(salt, user_id))[0..16)
std::string hash_player(std::string_view salt, std::string_view user_id);
}
