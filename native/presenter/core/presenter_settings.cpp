#include "presenter_settings.hpp"
#include <cmath>
#include <stdexcept>

namespace odeum::presenter {
namespace {
template<class T> void read(const Json& object, const char* key, T& target) {
    if (!object.contains(key)) return;
    try { target = object.at(key).get<T>(); }
    catch (const Json::exception&) { throw std::invalid_argument(std::string("Invalid settings value: ") + key); }
}
}

bool is_corner_name(std::string_view name) {
    return name == "top-left" || name == "top-right" || name == "bottom-left" || name == "bottom-right";
}

PresenterSettings settings_from_json(const Json& json) {
    if (!json.is_object()) throw std::invalid_argument("Settings must be a JSON object");
    PresenterSettings s;
    if (json.contains("stream")) {
        const auto& j = json.at("stream");
        if (!j.is_object()) throw std::invalid_argument("Invalid settings value: stream");
        read(j, "width", s.stream.width);
        read(j, "height", s.stream.height);
        read(j, "fps", s.stream.fps);
        read(j, "max_bitrate_kbps", s.stream.max_bitrate_kbps);
        read(j, "keyframe_interval_s", s.stream.keyframe_interval_s);
        read(j, "audio", s.stream.audio);
        read(j, "audio_bitrate_kbps", s.stream.audio_bitrate_kbps);
    }
    validate(s.stream);
    read(json, "font", s.font);
    read(json, "comments_visible", s.comments_visible);
    if (json.contains("overlay")) {
        const auto& j = json.at("overlay");
        if (!j.is_object()) throw std::invalid_argument("Invalid settings value: overlay");
        read(j, "corner", s.overlay.corner);
        read(j, "margin_x", s.overlay.margin_x);
        read(j, "margin_y", s.overlay.margin_y);
        read(j, "absolute", s.overlay.absolute);
        read(j, "x", s.overlay.x);
        read(j, "y", s.overlay.y);
    }
    if (!is_corner_name(s.overlay.corner)) throw std::invalid_argument("Invalid settings value: overlay.corner");
    for (float margin : {s.overlay.margin_x, s.overlay.margin_y})
        if (!std::isfinite(margin) || margin < 0 || margin > 1000) throw std::invalid_argument("Invalid settings value: overlay margin");
    return s;
}

Json settings_to_json(const PresenterSettings& s) {
    return {
        {"stream", {{"width", s.stream.width}, {"height", s.stream.height}, {"fps", s.stream.fps},
                    {"max_bitrate_kbps", s.stream.max_bitrate_kbps}, {"keyframe_interval_s", s.stream.keyframe_interval_s},
                    {"audio", s.stream.audio}, {"audio_bitrate_kbps", s.stream.audio_bitrate_kbps}}},
        {"font", s.font},
        {"comments_visible", s.comments_visible},
        {"overlay", {{"corner", s.overlay.corner}, {"margin_x", s.overlay.margin_x}, {"margin_y", s.overlay.margin_y},
                     {"absolute", s.overlay.absolute}, {"x", s.overlay.x}, {"y", s.overlay.y}}},
    };
}
}
