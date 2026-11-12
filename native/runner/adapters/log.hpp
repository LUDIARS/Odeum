#pragma once
// One JSON object per line on stdout (the former pino output). Callers pass only operational
// fields: never tokens, request bodies, SDP, salts or environment values.
#include "../core/json.hpp"
#include <string_view>

namespace odeum::runner::log {
enum class Level { debug = 20, info = 30, warn = 40, error = 50 };
// CUSTOS_LOG_LEVEL=debug|info|warn|error; default info.
void set_level(Level level);
bool enabled(Level level);
void write(Level level, std::string_view component, std::string_view message, const Json& fields = Json::object());

inline void debug(std::string_view c, std::string_view m, const Json& f = Json::object()) { write(Level::debug, c, m, f); }
inline void info(std::string_view c, std::string_view m, const Json& f = Json::object()) { write(Level::info, c, m, f); }
inline void warn(std::string_view c, std::string_view m, const Json& f = Json::object()) { write(Level::warn, c, m, f); }
inline void error(std::string_view c, std::string_view m, const Json& f = Json::object()) { write(Level::error, c, m, f); }
}
