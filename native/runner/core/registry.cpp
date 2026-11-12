#include "registry.hpp"

namespace odeum::runner {
const char* to_string(RunnerKind kind) {
    switch (kind) {
        case RunnerKind::build: return "build";
        case RunnerKind::run: return "run";
        case RunnerKind::test: return "test";
    }
    return "run";
}

const char* to_string(Lifecycle lifecycle) {
    switch (lifecycle) {
        case Lifecycle::idle: return "idle";
        case Lifecycle::building: return "building";
        case Lifecycle::built: return "built";
        case Lifecycle::running: return "running";
        case Lifecycle::stopped: return "stopped";
        case Lifecycle::crashed: return "crashed";
        case Lifecycle::killed: return "killed";
        case Lifecycle::testing: return "testing";
    }
    return "idle";
}

namespace {
template<class T> Json nullable(const std::optional<T>& value) { return value ? Json(*value) : Json(nullptr); }
}

Json to_json(const AppStatus& s) {
    return Json{{"id", s.id}, {"lifecycle", to_string(s.lifecycle)}, {"pid", nullable(s.pid)}, {"lastExitCode", nullable(s.last_exit_code)},
        {"lastChangeAt", s.last_change_at}, {"lastBuildId", nullable(s.last_build_id)}, {"lastRunId", nullable(s.last_run_id)},
        {"lastTestId", nullable(s.last_test_id)}};
}

AppsRegistry::AppsRegistry(std::vector<AppConfig> apps, std::function<long long()> now_ms) : apps_(std::move(apps)), now_(std::move(now_ms)) {
    for (const auto& app : apps_) {
        AppStatus status;
        status.id = app.id;
        status.last_change_at = now_();
        status_.emplace(app.id, std::move(status));
    }
}

const AppConfig* AppsRegistry::config(const std::string& id) const {
    for (const auto& app : apps_) if (app.id == id) return &app;
    return nullptr;
}

const AppStatus* AppsRegistry::status(const std::string& id) const {
    auto it = status_.find(id);
    return it == status_.end() ? nullptr : &it->second;
}

bool AppsRegistry::set_lifecycle(const std::string& id, Lifecycle next, const StatusPatch& patch) {
    auto it = status_.find(id);
    if (it == status_.end()) return false;
    auto& s = it->second;
    if (patch.pid) s.pid = *patch.pid;
    if (patch.last_exit_code) s.last_exit_code = *patch.last_exit_code;
    if (patch.last_build_id) s.last_build_id = *patch.last_build_id;
    if (patch.last_run_id) s.last_run_id = *patch.last_run_id;
    if (patch.last_test_id) s.last_test_id = *patch.last_test_id;
    s.lifecycle = next;
    s.last_change_at = now_();
    const AppStatus snapshot = s;  // listeners may re-enter the registry
    for (const auto& listener : listeners_) listener(id, snapshot);
    return true;
}
}
