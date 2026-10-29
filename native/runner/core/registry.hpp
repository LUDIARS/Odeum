#pragma once
// In-memory app registry: the loaded configuration and each app's lifecycle status, the single
// source of truth for API and WebSocket answers. Port of custos/src/apps/registry.ts.
#include "app_config.hpp"
#include <functional>
#include <map>

namespace odeum::runner {
enum class RunnerKind { build, run, test };
const char* to_string(RunnerKind kind);

// idle | building | built | running | stopped | crashed | killed | testing
enum class Lifecycle { idle, building, built, running, stopped, crashed, killed, testing };
const char* to_string(Lifecycle lifecycle);

struct AppStatus {
    std::string id;
    Lifecycle lifecycle = Lifecycle::idle;
    std::optional<long long> pid;
    std::optional<int> last_exit_code;
    long long last_change_at = 0;  // epoch ms
    std::optional<std::string> last_build_id, last_run_id, last_test_id;
};
Json to_json(const AppStatus& status);

struct StatusPatch {
    std::optional<std::optional<long long>> pid;
    std::optional<std::optional<int>> last_exit_code;
    std::optional<std::optional<std::string>> last_build_id, last_run_id, last_test_id;
};

class AppsRegistry {
public:
    using Listener = std::function<void(const std::string& app_id, const AppStatus& status)>;
    AppsRegistry(std::vector<AppConfig> apps, std::function<long long()> now_ms);
    const std::vector<AppConfig>& configs() const { return apps_; }
    const AppConfig* config(const std::string& id) const;
    const AppStatus* status(const std::string& id) const;
    // False for an unknown app id. Notifies every listener with the merged status.
    bool set_lifecycle(const std::string& id, Lifecycle next, const StatusPatch& patch = {});
    void on_status_changed(Listener listener) { listeners_.push_back(std::move(listener)); }
private:
    std::vector<AppConfig> apps_;
    std::map<std::string, AppStatus> status_;
    std::function<long long()> now_;
    std::vector<Listener> listeners_;
};
}
