#pragma once
// Runtime overrides of the screenshot auto-stream (custos/src/runtime/stream-prefs.ts). In memory
// only: a restart returns to apps.json. Patch semantics: value = set, null = clear, absent = keep.
#include "json.hpp"
#include <functional>
#include <optional>
#include <variant>

namespace odeum::runner {
struct StreamPrefs {
    std::optional<double> interval_sec;
    std::optional<long long> max_width;
};

struct StreamPrefsPatch {
    // monostate = absent, nullptr = clear, value = set
    std::variant<std::monostate, std::nullptr_t, double> interval_sec;
    std::variant<std::monostate, std::nullptr_t, long long> max_width;
};
// Validates a PUT /api/settings/stream body. The error names the offending field.
std::variant<StreamPrefsPatch, std::string> parse_stream_prefs_patch(const Json& body);

class RuntimeStreamPrefs {
public:
    StreamPrefs get() const { return state_; }
    Json to_json() const { return json_; }
    StreamPrefs apply(const StreamPrefsPatch& patch);
    void reset();
    void on_change(std::function<void(const StreamPrefs&)> listener) { listeners_.push_back(std::move(listener)); }
private:
    StreamPrefs state_;
    Json json_ = Json::object();  // keeps the JavaScript key order of the former object
    std::vector<std::function<void(const StreamPrefs&)>> listeners_;
};
}
