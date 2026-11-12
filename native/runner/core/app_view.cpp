#include "app_view.hpp"

namespace odeum::runner {
Json redact(const AppConfig& app) {
    return Json{
        {"id", app.id},
        {"name", app.name},
        {"description", app.description.value_or("")},
        {"target", app.target},
        {"hasBuild", app.build.has_value()},
        {"hasTest", app.test.has_value()},
        {"hasUnityBridge", app.in_app_bridge && app.in_app_bridge->kind == "unity"},
        {"hasVideo", app.capture.has_value() || app.in_app_bridge.has_value()},
        {"hasAudio", app.audio},
        {"capture", app.capture ? Json{{"type", app.capture->type}, {"fps", app.capture->fps}} : Json(nullptr)},
        {"input", to_json(app.input)},
        {"playLog", Json{{"enabled", app.play_log && app.play_log->enabled}}},
    };
}
}
