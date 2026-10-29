#include "stream_prefs.hpp"
#include <cmath>

namespace odeum::runner {
std::variant<StreamPrefsPatch, std::string> parse_stream_prefs_patch(const Json& body) {
    if (!body.is_object()) return std::string("Expected object");
    StreamPrefsPatch patch;
    if (auto it = body.find("intervalSec"); it != body.end()) {
        if (it->is_null()) patch.interval_sec = nullptr;
        else {
            if (!it->is_number()) return std::string("intervalSec: Expected number");
            const double v = it->get<double>();
            if (!std::isfinite(v) || v < 0 || v > 60) return std::string("intervalSec: Number must be between 0 and 60");
            patch.interval_sec = v;
        }
    }
    if (auto it = body.find("maxWidth"); it != body.end()) {
        if (it->is_null()) patch.max_width = nullptr;
        else {
            if (!it->is_number()) return std::string("maxWidth: Expected number");
            const double v = it->get<double>();
            if (!std::isfinite(v) || std::floor(v) != v) return std::string("maxWidth: Expected integer");
            if (v < 0 || v > 7680) return std::string("maxWidth: Number must be between 0 and 7680");
            patch.max_width = static_cast<long long>(v);
        }
    }
    return patch;
}

StreamPrefs RuntimeStreamPrefs::apply(const StreamPrefsPatch& patch) {
    const StreamPrefs before = state_;
    if (std::holds_alternative<std::nullptr_t>(patch.interval_sec)) { state_.interval_sec.reset(); json_.erase("intervalSec"); }
    else if (auto v = std::get_if<double>(&patch.interval_sec)) { state_.interval_sec = *v; json_["intervalSec"] = js_number(*v); }
    if (std::holds_alternative<std::nullptr_t>(patch.max_width)) { state_.max_width.reset(); json_.erase("maxWidth"); }
    else if (auto v = std::get_if<long long>(&patch.max_width)) { state_.max_width = *v; json_["maxWidth"] = *v; }
    const bool changed = before.interval_sec != state_.interval_sec || before.max_width != state_.max_width;
    if (changed) for (const auto& listener : listeners_) listener(state_);
    return state_;
}

void RuntimeStreamPrefs::reset() {
    const bool had = state_.interval_sec || state_.max_width;
    state_ = {};
    json_ = Json::object();
    if (had) for (const auto& listener : listeners_) listener(state_);
}
}
