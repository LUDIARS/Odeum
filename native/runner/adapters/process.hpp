#pragma once
// Child processes without a shell: program + argument array, explicit working directory and
// environment overrides, asynchronous pipes on the caller's io_context, and termination of the
// whole process tree (POSIX process group / Windows Job Object).
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace odeum::runner {
struct ProcessSpec {
    std::string program;  // bare name (PATH), relative to cwd, or absolute
    std::vector<std::string> args;
    std::string cwd;      // empty: the runner's working directory
    std::vector<std::pair<std::string, std::string>> env;  // added to / replacing the runner's environment
    bool stdin_pipe = false;
    bool stdout_pipe = true;
    bool stderr_pipe = true;
    bool hide_window = false;  // Windows: no console window, SW_HIDE (Node's windowsHide)
};

// Exactly one of code / signal, as Node's "exit" event reports them.
struct ExitStatus {
    std::optional<int> code;
    std::optional<std::string> signal;
};

// Node-style "spawn <program> ENOENT" / EINVAL / EACCES. The program name is operator configuration.
class SpawnError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ChildProcess : public std::enable_shared_from_this<ChildProcess> {
public:
    struct Handlers {
        std::function<void(std::string_view)> on_stdout, on_stderr;  // raw bytes, in order
        std::function<void()> on_stdout_end, on_stderr_end;
        std::function<void(const ExitStatus&)> on_exit;  // once, on the io_context
    };
    // Throws SpawnError. Handlers run on `io`.
    static std::shared_ptr<ChildProcess> spawn(boost::asio::io_context& io, const ProcessSpec& spec, Handlers handlers);
    virtual ~ChildProcess() = default;
    virtual long long pid() const = 0;
    virtual bool running() const = 0;
    // SIGKILL to the process group / TerminateJobObject. No-op after exit.
    virtual void kill_tree() = 0;
    // SIGTERM to the process group / TerminateJobObject. No-op after exit.
    virtual void terminate() = 0;
    // Writes to stdin in order; `done(false)` when the pipe broke or the queue limit was hit.
    virtual void write_stdin(std::shared_ptr<const std::string> data, std::function<void(bool)> done) = 0;
    virtual std::size_t stdin_queued_bytes() const = 0;
    virtual void close_stdin() = 0;
};

struct CollectedOutput {
    ExitStatus status;
    std::string stdout_bytes;
    std::string stderr_tail;  // the last bytes only (diagnostics)
    bool timed_out = false;
    bool overflow = false;    // stdout exceeded the budget; the process was killed
};
using CollectResult = std::variant<CollectedOutput, SpawnError>;

// Runs to completion: stdout is charged against a Lorica budget of `stdout_limit` bytes (the
// process tree is killed beyond it), stderr keeps its last `stderr_tail` bytes, and the tree is
// killed after `timeout`. `stdin_data` (if any) is written and stdin closed.
void run_collect(boost::asio::io_context& io, ProcessSpec spec, std::chrono::milliseconds timeout, std::size_t stdout_limit,
    std::size_t stderr_tail, std::optional<std::string> stdin_data, std::function<void(CollectResult)> done);

// Platform helpers shared with the tests.
std::string quote_windows_argument(const std::string& argument);  // MSVCRT command-line rules
bool is_windows_batch_file(const std::string& path);
}
