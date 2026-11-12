#include "apps_runner.hpp"
#include "../adapters/log.hpp"
#include "../core/json.hpp"
#include <algorithm>
#include <filesystem>
#include <cctype>

namespace odeum::runner {
namespace {
constexpr std::size_t max_line_bytes = 64 * 1024;

bool is_absolute_path(const std::string& path, bool windows_paths) {
    if (path.empty()) return false;
    if (path.front() == '/') return true;
    if (windows_paths) {
        if (path.front() == '\\') return true;
        if (path.size() > 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) return true;
    }
    return false;
}

std::string join(const std::string& dir, const std::string& name, bool windows_paths) {
    if (dir.empty()) return name;
    const char last = dir.back();
    const char separator = windows_paths ? '\\' : '/';
    return (last == '/' || last == '\\') ? dir + name : dir + separator + name;
}
}

std::string resolve_bare_exe(const std::string& cmd, const std::string& cwd, const std::string& runner_dir, bool windows_paths) {
    if (is_absolute_path(cmd, windows_paths)) return cmd;
    if (cmd.find('/') != std::string::npos || cmd.find('\\') != std::string::npos) return cmd;
    std::string lower = cmd;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool bare_exe = lower.size() > 4 && (lower.ends_with(".exe") || lower.ends_with(".bat") || lower.ends_with(".cmd") || lower.ends_with(".com"));
    if (!bare_exe) return cmd;
    const auto dir = is_absolute_path(cwd, windows_paths) ? cwd : join(runner_dir, cwd, windows_paths);
    return join(dir, cmd, windows_paths);
}

std::vector<std::string> LineSplitter::push(std::string_view chunk) {
    std::vector<std::string> lines;
    partial_.append(chunk);
    std::size_t start = 0;
    for (;;) {
        const auto newline = partial_.find('\n', start);
        if (newline == std::string::npos) break;
        auto end = newline;
        if (end > start && partial_[end - 1] == '\r') --end;
        lines.push_back(sanitize_utf8(std::string_view(partial_).substr(start, end - start)));
        start = newline + 1;
    }
    partial_.erase(0, start);
    while (partial_.size() > max_line_bytes) {
        lines.push_back(sanitize_utf8(std::string_view(partial_).substr(0, max_line_bytes)));
        partial_.erase(0, max_line_bytes);
    }
    return lines;
}

AppsRunner::AppsRunner(boost::asio::io_context& io, AppsRegistry& registry, Launcher launcher, std::function<std::string()> new_id)
    : io_(io), registry_(registry), launcher_(std::move(launcher)), new_id_(std::move(new_id)) {}

AppsRunner::~AppsRunner() { shutdown(); }

bool AppsRunner::is_active(const std::string& app_id, RunnerKind kind) const {
    auto it = active_.find(app_id);
    return it != active_.end() && it->second.contains(kind);
}

std::optional<long long> AppsRunner::run_pid(const std::string& app_id) const {
    auto it = active_.find(app_id);
    if (it == active_.end()) return std::nullopt;
    auto run = it->second.find(RunnerKind::run);
    if (run == it->second.end() || !run->second->process) return std::nullopt;
    return run->second->process->pid();
}

std::string AppsRunner::start_build(const AppConfig& app) {
    if (!app.build) throw RunnerError("app " + app.id + " has no build command");
    return start(app, RunnerKind::build, *app.build);
}

std::string AppsRunner::start_run(const AppConfig& app) { return start(app, RunnerKind::run, app.run); }

std::string AppsRunner::start_test(const AppConfig& app) {
    if (!app.test) throw RunnerError("app " + app.id + " has no test command");
    return start(app, RunnerKind::test, *app.test);
}

std::string AppsRunner::start(const AppConfig& app, RunnerKind kind, const CmdConfig& cmd) {
    if (is_active(app.id, kind)) throw RunnerError(std::string(to_string(kind)) + " already running for " + app.id);
#ifdef _WIN32
    constexpr bool windows_paths = true;
#else
    constexpr bool windows_paths = false;
#endif
    const auto op_id = new_id_();
    ProcessSpec spec;
    std::error_code ignored;
    spec.program = resolve_bare_exe(cmd.cmd, cmd.working_dir, std::filesystem::current_path(ignored).string(), windows_paths);
    spec.args = cmd.args;
    spec.cwd = cmd.working_dir;
    spec.env = cmd.env;
    spec.hide_window = true;
    log::info("runner", "spawn", {{"appId", app.id}, {"kind", to_string(kind)}, {"opId", op_id}, {"cwd", cmd.working_dir}, {"cmd", spec.program}, {"raw", cmd.cmd}});

    auto active = std::make_shared<Active>();
    active->kind = kind;
    active->op_id = op_id;
    const auto app_id = app.id;
    std::weak_ptr<Active> weak = active;
    // Output and exit arrive in any order (SIGCHLD may beat the last pipe read). The exit is reported
    // once both pipes reached EOF, or 2 s after the exit if a descendant keeps a pipe open, so the
    // last lines are delivered before "exit" and never dropped.
    struct Pending {
        LineSplitter out, err;
        bool out_done = false, err_done = false, reported = false;
        std::optional<ExitStatus> exit;
        std::unique_ptr<boost::asio::steady_timer> drain;
    };
    auto pending = std::make_shared<Pending>();
    auto report = [this, app_id, weak, pending] {
        if (pending->reported || !pending->exit) return;
        pending->reported = true;
        if (pending->drain) pending->drain->cancel();
        if (auto a = weak.lock()) exited(app_id, a, *pending->exit);
    };
    ChildProcess::Handlers handlers;
    handlers.on_stdout = [this, app_id, kind, pending](std::string_view chunk) {
        for (const auto& line : pending->out.push(chunk)) emit_log(app_id, kind, "stdout", line);
    };
    handlers.on_stderr = [this, app_id, kind, pending](std::string_view chunk) {
        for (const auto& line : pending->err.push(chunk)) emit_log(app_id, kind, "stderr", line);
    };
    handlers.on_stdout_end = [pending, report] { pending->out_done = true; if (pending->err_done) report(); };
    handlers.on_stderr_end = [pending, report] { pending->err_done = true; if (pending->out_done) report(); };
    handlers.on_exit = [this, pending, report](const ExitStatus& status) {
        pending->exit = status;
        if (pending->out_done && pending->err_done) return report();
        pending->drain = std::make_unique<boost::asio::steady_timer>(io_, std::chrono::seconds(2));
        pending->drain->async_wait([report](const boost::system::error_code& error) { if (!error) report(); });
    };
    try {
        active->process = launcher_(spec, std::move(handlers));
    } catch (const SpawnError& error) {
        // Reported to the caller; the former runner left the app stuck in "running" instead.
        log::error("runner", "spawn error", {{"appId", app.id}, {"kind", to_string(kind)}, {"error", error.what()}});
        emit_log(app.id, kind, "stderr", std::string("[custos] spawn error: ") + error.what());
        throw RunnerError(error.what());
    }
    active_[app.id][kind] = active;

    static constexpr Lifecycle start_state[] = {Lifecycle::building, Lifecycle::running, Lifecycle::testing};
    StatusPatch patch;
    patch.pid = active->process->pid();
    const auto* status = registry_.status(app.id);
    patch.last_build_id = kind == RunnerKind::build ? std::optional<std::string>(op_id) : (status ? status->last_build_id : std::nullopt);
    patch.last_run_id = kind == RunnerKind::run ? std::optional<std::string>(op_id) : (status ? status->last_run_id : std::nullopt);
    patch.last_test_id = kind == RunnerKind::test ? std::optional<std::string>(op_id) : (status ? status->last_test_id : std::nullopt);
    registry_.set_lifecycle(app.id, start_state[static_cast<int>(kind)], patch);

    if (cmd.timeout_sec && *cmd.timeout_sec > 0) {
        active->timer = std::make_unique<boost::asio::steady_timer>(io_);
        // Capped at 30 days: longer values only matter as "no practical limit".
        const auto seconds = std::min<long long>(*cmd.timeout_sec, 30LL * 24 * 3600);
        active->timer->expires_after(std::chrono::seconds(seconds));
        active->timer->async_wait([weak, app_id, kind, op_id](const boost::system::error_code& error) {
            if (error) return;
            if (auto a = weak.lock(); a && a->process) {
                log::warn("runner", "timeout — sending SIGKILL", {{"appId", app_id}, {"kind", to_string(kind)}, {"opId", op_id}});
                a->process->kill_tree();
            }
        });
    }
    return op_id;
}

void AppsRunner::emit_log(const std::string& app_id, RunnerKind kind, const char* stream, const std::string& line) {
    for (const auto& listener : log_listeners_) listener(app_id, kind, stream, line);
}

void AppsRunner::exited(const std::string& app_id, const std::shared_ptr<Active>& active, const ExitStatus& status) {
    if (active->timer) active->timer->cancel();
    if (auto it = active_.find(app_id); it != active_.end()) {
        auto kind_it = it->second.find(active->kind);
        if (kind_it != it->second.end() && kind_it->second == active) it->second.erase(kind_it);
        if (it->second.empty()) active_.erase(it);
    }
    for (const auto& listener : exit_listeners_) listener(app_id, active->kind, status);

    static constexpr Lifecycle normal_state[] = {Lifecycle::built, Lifecycle::stopped, Lifecycle::idle};
    auto next = normal_state[static_cast<int>(active->kind)];
    if (active->kind == RunnerKind::run) {
        if (active->killed) next = Lifecycle::killed;
        else if (status.signal == "SIGKILL" || status.signal == "SIGTERM") next = Lifecycle::killed;
        else if (status.code && *status.code != 0) next = Lifecycle::crashed;
    }
    if (active->kind == RunnerKind::build && status.code && *status.code != 0) next = Lifecycle::idle;
    StatusPatch patch;
    patch.pid = std::optional<long long>();
    patch.last_exit_code = status.code;
    registry_.set_lifecycle(app_id, next, patch);
}

bool AppsRunner::kill(const std::string& app_id) {
    auto it = active_.find(app_id);
    if (it == active_.end()) return false;
    auto run = it->second.find(RunnerKind::run);
    if (run == it->second.end() || !run->second->process || run->second->process->pid() <= 0) return false;
    run->second->killed = true;  // the exit handler reports "killed", not "crashed"
    run->second->process->kill_tree();
    return true;
}

void AppsRunner::shutdown() {
    for (auto& [app_id, kinds] : active_) {
        for (auto& [kind, active] : kinds) {
            if (active->process) active->process->terminate();
            if (active->timer) active->timer->cancel();
        }
    }
    active_.clear();
}
}
