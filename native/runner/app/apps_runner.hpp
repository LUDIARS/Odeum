#pragma once
// build / run / test processes of the registered apps (custos/src/apps/runner.ts). One process per
// (app, kind); output is delivered line by line; only `run` can be killed; a timeout kills the tree.
// The only place that starts or stops target-app processes.
#include "../adapters/process.hpp"
#include "../core/registry.hpp"
#include <boost/asio/steady_timer.hpp>

namespace odeum::runner {
class RunnerError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// "foo.exe" with cwd "build" -> "<runner dir>/build/foo.exe" (path.resolve): bare .exe/.bat/.cmd/.com
// names are looked up in the working directory, not on PATH. Paths and extension-less names are
// returned unchanged.
std::string resolve_bare_exe(const std::string& cmd, const std::string& cwd, const std::string& runner_dir, bool windows_paths);

// Splits process output into lines (\n or \r\n). An unterminated fragment waits for the next chunk;
// a fragment beyond 64 KiB is delivered as a line so a process cannot grow the buffer without bound.
class LineSplitter {
public:
    std::vector<std::string> push(std::string_view chunk);
private:
    std::string partial_;
};

class AppsRunner {
public:
    using Launcher = std::function<std::shared_ptr<ChildProcess>(const ProcessSpec&, ChildProcess::Handlers)>;
    using LogListener = std::function<void(const std::string& app_id, RunnerKind, const char* stream, const std::string& text)>;
    using ExitListener = std::function<void(const std::string& app_id, RunnerKind, const ExitStatus&)>;

    AppsRunner(boost::asio::io_context& io, AppsRegistry& registry, Launcher launcher, std::function<std::string()> new_id);
    ~AppsRunner();
    bool is_active(const std::string& app_id, RunnerKind kind) const;
    std::optional<long long> run_pid(const std::string& app_id) const;
    // Throw RunnerError ("<kind> already running for <id>", "app <id> has no build command", spawn errors).
    std::string start_build(const AppConfig& app);
    std::string start_run(const AppConfig& app);
    std::string start_test(const AppConfig& app);
    bool kill(const std::string& app_id);
    void shutdown();
    void on_log(LogListener listener) { log_listeners_.push_back(std::move(listener)); }
    void on_exit(ExitListener listener) { exit_listeners_.push_back(std::move(listener)); }

private:
    struct Active {
        RunnerKind kind;
        std::string op_id;
        std::shared_ptr<ChildProcess> process;
        std::unique_ptr<boost::asio::steady_timer> timer;
        bool killed = false;
    };
    boost::asio::io_context& io_;
    AppsRegistry& registry_;
    Launcher launcher_;
    std::function<std::string()> new_id_;
    std::map<std::string, std::map<RunnerKind, std::shared_ptr<Active>>> active_;
    std::vector<LogListener> log_listeners_;
    std::vector<ExitListener> exit_listeners_;
    std::string start(const AppConfig& app, RunnerKind kind, const CmdConfig& cmd);
    void emit_log(const std::string& app_id, RunnerKind kind, const char* stream, const std::string& line);
    void exited(const std::string& app_id, const std::shared_ptr<Active>& active, const ExitStatus& status);
};
}
