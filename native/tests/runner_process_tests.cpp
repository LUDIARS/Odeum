// custos/tests/runner-resolve.test.ts + AppsRunner lifecycle (runner.ts) + the process adapter
// (argument arrays, no shell, tree kill, output budget, deadline). POSIX processes (/bin/sh) are
// used as targets; the Windows adapter is compiled only on Windows.
#include "runner_check.hpp"
#include "runner_fake_http.hpp"
#include <adapters/process.hpp>
#include <app/apps_runner.hpp>
#include <filesystem>

using namespace odeum::runner;
using namespace runner_test;

namespace {
AppConfig app_with(const std::string& script, std::optional<long long> timeout = std::nullopt) {
    auto config = parse_apps_config(Json{{"apps", Json::array({Json{{"id", "demo"}, {"name", "Demo"},
        {"run", {{"cwd", "."}, {"cmd", "sh"}, {"args", {"-c", script}}}}, {"build", {{"cwd", "."}, {"cmd", "sh"}, {"args", {"-c", "exit 3"}}}},
        {"test", {{"cwd", "."}, {"cmd", "sh"}, {"args", {"-c", "exit 0"}}}}}})}}).apps.at(0);
    config.run.timeout_sec = timeout;
    return config;
}

struct Harness {
    boost::asio::io_context io;
    long long now = 0;
    int ids = 0;
    AppsRegistry registry;
    AppsRunner runner;
    std::vector<std::string> lines;
    std::vector<std::pair<std::string, ExitStatus>> exits;
    explicit Harness(const AppConfig& app)
        : registry({app}, [this] { return now; }),
          runner(io, registry, [this](const ProcessSpec& spec, ChildProcess::Handlers h) { return ChildProcess::spawn(io, spec, std::move(h)); },
              [this] { return "op-" + std::to_string(++ids); }) {
        runner.on_log([this](const std::string&, RunnerKind kind, const char* stream, const std::string& text) { lines.push_back(std::string(to_string(kind)) + ":" + stream + ":" + text); });
        runner.on_exit([this](const std::string&, RunnerKind kind, const ExitStatus& s) { exits.emplace_back(to_string(kind), s); });
    }
    void wait_exits(std::size_t n) { run_until(io, [&] { return exits.size() >= n; }, std::chrono::milliseconds(8000)); }
};
}

int main(int argc, char** argv) {
    return run_all(argc, argv, "runner.process", {
        {"resolveBareExe > golden resolve_bare_exe_cases.json (POSIX paths)", [] {
            for (const auto& c : fixture("resolve_bare_exe_cases.json")) {
                const auto out = resolve_bare_exe(c["cmd"], c["cwd"], c["runnerDir"], false);
                check(out == c["output"].get<std::string>(), "resolve " + c.dump() + " -> " + out);
            }
        }},
        {"resolveBareExe > Windows paths stay absolute and bare names are promoted", [] {
            check(resolve_bare_exe("adventurecube.exe", "E:/Ars/build/Debug", "C:\\run", true) == "E:/Ars/build/Debug\\adventurecube.exe", "promoted");
            check(resolve_bare_exe("C:/tools/foo.exe", "/x", "C:\\run", true) == "C:/tools/foo.exe", "absolute");
            check(resolve_bare_exe("run.bat", "rel", "C:\\run", true) == "C:\\run\\rel\\run.bat", "relative cwd");
        }},
        {"process > Windows command-line quoting (MSVCRT rules)", [] {
            check(quote_windows_argument("") == "\"\"" && quote_windows_argument("plain") == "plain", "simple");
            check(quote_windows_argument("a b") == "\"a b\"" && quote_windows_argument("say \"hi\"") == "\"say \\\"hi\\\"\"", "spaces and quotes");
            check(quote_windows_argument("C:\\dir with space\\") == "\"C:\\dir with space\\\\\"", "trailing backslash doubled");
            check(quote_windows_argument("a\\\\\"b") == "\"a\\\\\\\\\\\"b\"", "backslashes before a quote");
            check(is_windows_batch_file("C:\\x\\RUN.BAT") && is_windows_batch_file("a.cmd") && !is_windows_batch_file("a.exe"), "batch detection");
        }},
        {"process > LineSplitter keeps fragments, handles CRLF and bounds a line", [] {
            LineSplitter s;
            check(s.push("a\r\nb").size() == 1, "one line");
            const auto rest = s.push("c\n\n");
            check(rest.size() == 2 && rest[0] == "bc" && rest[1].empty(), "fragment joined");
            const auto huge = s.push(std::string(70 * 1024, 'x'));
            check(huge.size() == 1 && huge[0].size() == 64 * 1024, "64 KiB bound");
            check(s.push("\xff\n").at(0).find("\xEF\xBF\xBD") != std::string::npos, "invalid UTF-8 replaced");
        }},
        {"process > argument array without a shell, cwd and environment", [] {
            boost::asio::io_context io;
            std::optional<CollectResult> result;
            ProcessSpec spec{"sh", {"-c", "printf '%s|' \"$1\" \"$X\" \"$(pwd)\"", "sh", "a b; echo injected"}, "/tmp", {{"X", "y"}}, false, true, true, false};
            run_collect(io, spec, std::chrono::seconds(5), 4096, 256, std::nullopt, [&](CollectResult r) { result = std::move(r); });
            run_until(io, [&] { return result.has_value(); });
            const auto& out = std::get<CollectedOutput>(*result);
            check(out.stdout_bytes == "a b; echo injected|y|/tmp|", "got " + out.stdout_bytes);
            check(out.status.code == 0, "exit 0");
        }},
        {"process > missing program is a spawn error (ENOENT)", [] {
            boost::asio::io_context io;
            std::optional<CollectResult> result;
            run_collect(io, ProcessSpec{"definitely-not-a-program-xyz", {}, {}, {}, false, true, true, false}, std::chrono::seconds(5), 64, 64, std::nullopt,
                [&](CollectResult r) { result = std::move(r); });
            run_until(io, [&] { return result.has_value(); });
            check(std::holds_alternative<SpawnError>(*result), "spawn error");
            check(std::string(std::get<SpawnError>(*result).what()) == "spawn definitely-not-a-program-xyz ENOENT", "Node-style message");
        }},
        {"boundary > child output budget: exactly at the limit, limit + 1 kills the tree", [] {
            boost::asio::io_context io;
            auto run = [&](const char* script) {
                std::optional<CollectResult> result;
                run_collect(io, ProcessSpec{"sh", {"-c", script}, {}, {}, false, true, true, false}, std::chrono::seconds(5), 1000, 64, std::nullopt,
                    [&](CollectResult r) { result = std::move(r); });
                run_until(io, [&] { return result.has_value(); });
                return std::get<CollectedOutput>(*result);
            };
            const auto exact = run("head -c 1000 /dev/zero");
            check(!exact.overflow && exact.stdout_bytes.size() == 1000, "exact");
            const auto over = run("head -c 1001 /dev/zero; sleep 5");
            check(over.overflow && over.stdout_bytes.size() <= 1000, "overflow");
            const auto endless = run("yes");
            check(endless.overflow, "endless output is stopped");
        }},
        {"boundary > deadline kills the whole process group", [] {
            boost::asio::io_context io;
            std::optional<CollectResult> result;
            const auto start = std::chrono::steady_clock::now();
            run_collect(io, ProcessSpec{"sh", {"-c", "sleep 30 & sleep 30; echo done"}, {}, {}, false, true, true, false}, std::chrono::milliseconds(300), 64, 64,
                std::nullopt, [&](CollectResult r) { result = std::move(r); });
            run_until(io, [&] { return result.has_value(); }, std::chrono::seconds(10));
            check(result.has_value(), "finished");
            const auto& out = std::get<CollectedOutput>(*result);
            check(out.timed_out && out.status.signal == "SIGKILL", "killed by the deadline");
            check(std::chrono::steady_clock::now() - start < std::chrono::seconds(5), "the background child did not hold the pipe open");
        }},
        {"process > stdin is written and closed", [] {
            boost::asio::io_context io;
            std::optional<CollectResult> result;
            run_collect(io, ProcessSpec{"cat", {}, {}, {}, true, true, true, false}, std::chrono::seconds(5), 1 << 20, 64, std::string(200'000, 'q'),
                [&](CollectResult r) { result = std::move(r); });
            run_until(io, [&] { return result.has_value(); });
            check(std::get<CollectedOutput>(*result).stdout_bytes.size() == 200'000, "round trip through a pipe");
        }},
        {"AppsRunner > run streams lines, reaches stopped, keeps ids", [] {
            Harness h(app_with("echo hello; echo err 1>&2; printf partial"));
            const auto op = h.runner.start_run(h.registry.configs()[0]);
            check(op == "op-1" && h.registry.status("demo")->lifecycle == Lifecycle::running && h.registry.status("demo")->pid, "running with pid");
            h.wait_exits(1);
            check(h.registry.status("demo")->lifecycle == Lifecycle::stopped && h.registry.status("demo")->last_exit_code == 0, "stopped");
            check(!h.registry.status("demo")->pid && h.registry.status("demo")->last_run_id == "op-1", "pid cleared, run id kept");
            check(std::find(h.lines.begin(), h.lines.end(), "run:stdout:hello") != h.lines.end(), "stdout line");
            check(std::find(h.lines.begin(), h.lines.end(), "run:stderr:err") != h.lines.end(), "stderr line");
            check(std::find(h.lines.begin(), h.lines.end(), "run:stdout:partial") == h.lines.end(), "unterminated fragment is not a line");
        }},
        {"AppsRunner > one process per kind; build failure returns to idle; test returns to idle", [] {
            Harness h(app_with("sleep 5"));
            h.runner.start_run(h.registry.configs()[0]);
            throws([&] { h.runner.start_run(h.registry.configs()[0]); }, "run already running");
            check(h.runner.kill("demo"), "kill");
            h.wait_exits(1);
            check(h.registry.status("demo")->lifecycle == Lifecycle::killed, "killed");
            h.runner.start_build(h.registry.configs()[0]);
            h.wait_exits(2);
            check(h.registry.status("demo")->lifecycle == Lifecycle::idle && h.registry.status("demo")->last_exit_code == 3, "failed build -> idle");
            h.runner.start_test(h.registry.configs()[0]);
            h.wait_exits(3);
            check(h.registry.status("demo")->lifecycle == Lifecycle::idle, "test -> idle");
            check(!h.runner.kill("demo"), "nothing to kill");
        }},
        {"AppsRunner > non-zero exit is crashed; timeout kills; spawn failure is reported, not stuck", [] {
            Harness crash(app_with("exit 7"));
            crash.runner.start_run(crash.registry.configs()[0]);
            crash.wait_exits(1);
            check(crash.registry.status("demo")->lifecycle == Lifecycle::crashed && crash.registry.status("demo")->last_exit_code == 7, "crashed");
            Harness slow(app_with("sleep 10", 1));
            slow.runner.start_run(slow.registry.configs()[0]);
            slow.wait_exits(1);
            check(slow.registry.status("demo")->lifecycle == Lifecycle::killed, "timeout -> killed (SIGKILL)");
            auto missing = app_with("x");
            missing.run.cmd = "no-such-binary-xyz";
            Harness h(missing);
            throws([&] { h.runner.start_run(h.registry.configs()[0]); }, "spawn failure");
            check(!h.runner.is_active("demo", RunnerKind::run) && h.registry.status("demo")->lifecycle == Lifecycle::idle, "not stuck in running");
            check(!h.lines.empty() && h.lines.back().find("[custos] spawn error: spawn no-such-binary-xyz ENOENT") != std::string::npos, "log line as before");
        }},
    });
}
