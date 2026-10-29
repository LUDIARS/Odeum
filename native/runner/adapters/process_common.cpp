#include "process.hpp"
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <lorica/input_budget.hpp>
#include <algorithm>
#include <cctype>

namespace odeum::runner {
std::string quote_windows_argument(const std::string& argument) {
    if (argument.empty()) return "\"\"";
    if (argument.find_first_of(" \t\n\v\"") == std::string::npos) return argument;
    std::string out = "\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < argument.size() && argument[i] == '\\') { ++i; ++backslashes; }
        if (i == argument.size()) { out.append(backslashes * 2, '\\'); break; }
        if (argument[i] == '"') { out.append(backslashes * 2 + 1, '\\'); out.push_back('"'); }
        else { out.append(backslashes, '\\'); out.push_back(argument[i]); }
    }
    out.push_back('"');
    return out;
}

bool is_windows_batch_file(const std::string& path) {
    if (path.size() < 4) return false;
    std::string ext = path.substr(path.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".bat" || ext == ".cmd";
}

namespace {
struct Collector : std::enable_shared_from_this<Collector> {
    boost::asio::io_context& io;
    boost::asio::steady_timer timer, drain;
    lorica::input_budget budget;
    std::size_t tail_limit;
    CollectedOutput output;
    std::shared_ptr<ChildProcess> child;
    std::function<void(CollectResult)> done;
    std::shared_ptr<Collector> keep_alive;  // set while the process runs; released by finish()
    bool exited = false, stdout_closed = false, finished = false;

    Collector(boost::asio::io_context& context, std::size_t stdout_limit, std::size_t tail, std::function<void(CollectResult)> callback)
        : io(context), timer(context), drain(context), budget({stdout_limit, stdout_limit, stdout_limit, 1}), tail_limit(tail), done(std::move(callback)) {}

    void finish() {
        if (finished) return;
        finished = true;
        timer.cancel();
        drain.cancel();
        auto callback = std::move(done);
        auto self = std::move(keep_alive);
        child.reset();
        callback(std::move(output));
    }
    void maybe_finish() {
        if (exited && stdout_closed) finish();
    }
};
}

void run_collect(boost::asio::io_context& io, ProcessSpec spec, std::chrono::milliseconds timeout, std::size_t stdout_limit,
    std::size_t stderr_tail, std::optional<std::string> stdin_data, std::function<void(CollectResult)> done) {
    auto state = std::make_shared<Collector>(io, stdout_limit, stderr_tail, std::move(done));
    spec.stdin_pipe = stdin_data.has_value();
    spec.stdout_pipe = true;
    spec.stderr_pipe = true;
    std::weak_ptr<Collector> weak = state;
    ChildProcess::Handlers handlers;
    handlers.on_stdout = [weak](std::string_view chunk) {
        auto s = weak.lock();
        if (!s || s->finished || s->output.overflow) return;
        // Charged before it is kept: the process tree is killed past the budget.
        if (!s->budget.consume(chunk.size(), 0, 0)) {
            s->output.overflow = true;
            if (s->child) s->child->kill_tree();
            return;
        }
        s->output.stdout_bytes.append(chunk);
    };
    handlers.on_stderr = [weak](std::string_view chunk) {
        auto s = weak.lock();
        if (!s) return;
        s->output.stderr_tail.append(chunk);
        if (s->output.stderr_tail.size() > s->tail_limit) s->output.stderr_tail.erase(0, s->output.stderr_tail.size() - s->tail_limit);
    };
    handlers.on_stdout_end = [weak] {
        if (auto s = weak.lock()) { s->stdout_closed = true; s->maybe_finish(); }
    };
    handlers.on_exit = [weak](const ExitStatus& status) {
        auto s = weak.lock();
        if (!s) return;
        s->output.status = status;
        s->exited = true;
        // A descendant may keep stdout open; do not wait for it longer than this.
        s->drain.expires_after(std::chrono::seconds(2));
        s->drain.async_wait([weak](const boost::system::error_code& error) {
            if (error) return;
            if (auto c = weak.lock()) c->finish();
        });
        s->maybe_finish();
    };
    try {
        state->child = ChildProcess::spawn(io, spec, std::move(handlers));
    } catch (const SpawnError& error) {
        auto callback = std::move(state->done);
        state->finished = true;
        boost::asio::post(io, [callback, error] { callback(error); });
        return;
    }
    state->keep_alive = state;
    if (stdin_data) {
        state->child->write_stdin(std::make_shared<const std::string>(std::move(*stdin_data)), nullptr);
        state->child->close_stdin();
    }
    state->timer.expires_after(timeout);
    state->timer.async_wait([state](const boost::system::error_code& error) {
        if (error || state->finished) return;
        state->output.timed_out = true;
        if (state->child) state->child->kill_tree();
    });
}
}
