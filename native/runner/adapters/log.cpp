#include "log.hpp"
#include "clock.hpp"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace odeum::runner::log {
namespace {
std::atomic<int> threshold{static_cast<int>(Level::info)};
std::mutex output;
}

void set_level(Level level) { threshold = static_cast<int>(level); }
bool enabled(Level level) { return static_cast<int>(level) >= threshold.load(); }

void write(Level level, std::string_view component, std::string_view message, const Json& fields) {
    if (!enabled(level)) return;
    Json line{{"level", static_cast<int>(level)}, {"time", epoch_ms()}, {"component", component}, {"msg", message}};
    if (fields.is_object()) for (const auto& [key, value] : fields.items()) line[key] = value;
    const auto text = dump(line) + "\n";
    std::lock_guard lock(output);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}
}
