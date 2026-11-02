#ifndef _WIN32
#include "process.hpp"
#include <boost/asio/post.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <map>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace odeum::runner {
namespace {
namespace asio = boost::asio;
constexpr std::size_t stdin_queue_limit = 256u * 1024 * 1024;

const char* errno_name(int error) {
    switch (error) {
        case ENOENT: return "ENOENT";
        case EACCES: return "EACCES";
        case ENOTDIR: return "ENOTDIR";
        case EINVAL: return "EINVAL";
        case ENOEXEC: return "ENOEXEC";
        case ENOMEM: return "ENOMEM";
        case EAGAIN: return "EAGAIN";
        case EMFILE: return "EMFILE";
        case E2BIG: return "E2BIG";
        case EPERM: return "EPERM";
        default: return "EUNKNOWN";
    }
}

std::string signal_name(int signal) {
    switch (signal) {
        case SIGKILL: return "SIGKILL";
        case SIGTERM: return "SIGTERM";
        case SIGINT: return "SIGINT";
        case SIGHUP: return "SIGHUP";
        case SIGQUIT: return "SIGQUIT";
        case SIGABRT: return "SIGABRT";
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGFPE: return "SIGFPE";
        case SIGILL: return "SIGILL";
        case SIGPIPE: return "SIGPIPE";
        case SIGTRAP: return "SIGTRAP";
        case SIGALRM: return "SIGALRM";
        case SIGUSR1: return "SIGUSR1";
        case SIGUSR2: return "SIGUSR2";
        default: return "SIG" + std::to_string(signal);
    }
}

// Reaps children on SIGCHLD for one io_context. Only pids registered here are waited for.
class ChildWatcher : public asio::execution_context::service {
public:
    static inline asio::execution_context::id id;
    explicit ChildWatcher(asio::execution_context& context)
        : service(context), io_(static_cast<asio::io_context&>(context)), signals_(io_, SIGCHLD), poll_(io_) { arm(); }
    void watch(pid_t pid, std::function<void(int)> on_exit) {
        children_[pid] = std::move(on_exit);
        asio::post(io_, [this] { reap(); });  // the child may already have exited
        if (children_.size() == 1) poll();
    }
private:
    asio::io_context& io_;
    asio::signal_set signals_;
    asio::steady_timer poll_;
    std::map<pid_t, std::function<void(int)>> children_;
    void shutdown() override {
        boost::system::error_code ignored;
        signals_.cancel(ignored);
        poll_.cancel();
        children_.clear();
    }
    // SIGCHLD is the normal path. A slow poll while children run covers a lost or coalesced signal
    // (seen with sanitizer runtimes intercepting posix_spawn), so an exit is never missed.
    void poll() {
        poll_.expires_after(std::chrono::milliseconds(500));
        poll_.async_wait([this](const boost::system::error_code& error) {
            if (error) return;
            reap();
            if (!children_.empty()) poll();
        });
    }
    void arm() {
        signals_.async_wait([this](const boost::system::error_code& error, int) {
            if (error) return;
            reap();
            arm();
        });
    }
    void reap() {
        for (auto it = children_.begin(); it != children_.end();) {
            int status = 0;
            const pid_t done = ::waitpid(it->first, &status, WNOHANG);
            if (done == it->first || (done < 0 && errno == ECHILD)) {
                auto callback = std::move(it->second);
                it = children_.erase(it);
                if (callback) callback(done < 0 ? 0 : status);
            } else {
                ++it;
            }
        }
    }
};

struct Pipe {
    int read = -1, write = -1;
};

Pipe make_pipe() {
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) throw SpawnError(std::string("pipe ") + errno_name(errno));
    return {fds[0], fds[1]};
}

void close_fd(int& fd) {
    if (fd >= 0) ::close(fd);
    fd = -1;
}

std::vector<std::string> merged_environment(const ProcessSpec& spec) {
    std::vector<std::string> env;
    for (char** e = environ; e && *e; ++e) env.emplace_back(*e);
    for (const auto& [name, value] : spec.env) {
        const auto prefix = name + "=";
        bool replaced = false;
        for (auto& entry : env) {
            if (entry.compare(0, prefix.size(), prefix) == 0) { entry = prefix + value; replaced = true; break; }
        }
        if (!replaced) env.push_back(prefix + value);
    }
    return env;
}

bool executable_file(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(path.c_str(), X_OK) == 0;
}

// execvp after chdir(cwd): posix_spawn applies the chdir action before exec, so a name with a
// slash is passed unchanged and resolves against the child's directory. Bare names search PATH of
// the child's environment; relative PATH entries are relative to the child's directory too.
std::string resolve_program(const ProcessSpec& spec, const std::vector<std::string>& env) {
    const auto& program = spec.program;
    if (program.empty()) throw SpawnError("spawn ENOENT");
    if (program.find('/') != std::string::npos) return program;
    std::string path = "/usr/local/bin:/usr/bin:/bin";
    for (const auto& entry : env) if (entry.rfind("PATH=", 0) == 0) path = entry.substr(5);
    std::size_t start = 0;
    while (start <= path.size()) {
        auto end = path.find(':', start);
        if (end == std::string::npos) end = path.size();
        auto dir = path.substr(start, end - start);
        if (dir.empty()) dir = ".";
        const auto candidate = dir + "/" + program;
        const auto checked = (candidate.front() == '/' || spec.cwd.empty()) ? candidate : spec.cwd + "/" + candidate;
        if (executable_file(checked)) return candidate;
        start = end + 1;
    }
    throw SpawnError("spawn " + program + " ENOENT");
}

class PosixChild final : public ChildProcess {
public:
    PosixChild(asio::io_context& io, Handlers handlers) : io_(io), handlers_(std::move(handlers)) {}
    ~PosixChild() override {
        boost::system::error_code ignored;
        if (in_) in_->close(ignored);
        if (out_) out_->close(ignored);
        if (err_) err_->close(ignored);
    }

    void start(const ProcessSpec& spec) {
        const auto env = merged_environment(spec);
        const auto path = resolve_program(spec, env);
        Pipe in, out, err;
        int null_fd = -1;
        auto cleanup = [&] { close_fd(in.read); close_fd(in.write); close_fd(out.read); close_fd(out.write); close_fd(err.read); close_fd(err.write); close_fd(null_fd); };
        try {
            null_fd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
            if (null_fd < 0) throw SpawnError(std::string("open /dev/null ") + errno_name(errno));
            if (spec.stdin_pipe) in = make_pipe();
            if (spec.stdout_pipe) out = make_pipe();
            if (spec.stderr_pipe) err = make_pipe();
        } catch (...) { cleanup(); throw; }

        posix_spawn_file_actions_t actions;
        posix_spawnattr_t attributes;
        posix_spawn_file_actions_init(&actions);
        posix_spawnattr_init(&attributes);
        posix_spawn_file_actions_adddup2(&actions, spec.stdin_pipe ? in.read : null_fd, 0);
        posix_spawn_file_actions_adddup2(&actions, spec.stdout_pipe ? out.write : null_fd, 1);
        posix_spawn_file_actions_adddup2(&actions, spec.stderr_pipe ? err.write : null_fd, 2);
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
        // Listening and client sockets of the runner must not leak into target apps.
        posix_spawn_file_actions_addclosefrom_np(&actions, 3);
#endif
        if (!spec.cwd.empty()) posix_spawn_file_actions_addchdir_np(&actions, spec.cwd.c_str());
        sigset_t defaults, mask;
        sigemptyset(&mask);
        sigemptyset(&defaults);
        for (int s : {SIGPIPE, SIGCHLD, SIGINT, SIGTERM, SIGHUP}) sigaddset(&defaults, s);
        posix_spawnattr_setsigmask(&attributes, &mask);
        posix_spawnattr_setsigdefault(&attributes, &defaults);
        posix_spawnattr_setpgroup(&attributes, 0);  // own process group: kill_tree reaches descendants
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

        std::vector<char*> argv, envp;
        argv.push_back(const_cast<char*>(spec.program.c_str()));
        for (const auto& a : spec.args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        for (const auto& e : env) envp.push_back(const_cast<char*>(e.c_str()));
        envp.push_back(nullptr);

        pid_t pid = 0;
        const int result = ::posix_spawn(&pid, path.c_str(), &actions, &attributes, argv.data(), envp.data());
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        close_fd(in.read); close_fd(out.write); close_fd(err.write); close_fd(null_fd);
        if (result != 0) {
            close_fd(in.write); close_fd(out.read); close_fd(err.read);
            throw SpawnError("spawn " + spec.program + " " + errno_name(result));
        }
        pid_ = pid;
        running_ = true;
        if (in.write >= 0) in_.emplace(io_, in.write);
        if (out.read >= 0) out_.emplace(io_, out.read);
        if (err.read >= 0) err_.emplace(io_, err.read);
    }

    void begin() {
        auto self = std::static_pointer_cast<PosixChild>(shared_from_this());
        if (out_) read(*out_, true);
        if (err_) read(*err_, false);
        asio::use_service<ChildWatcher>(io_).watch(pid_, [weak = std::weak_ptr<PosixChild>(self)](int status) {
            if (auto child = weak.lock()) child->exited(status);
        });
    }

    long long pid() const override { return pid_; }
    bool running() const override { return running_; }
    void kill_tree() override { signal_group(SIGKILL); }
    void terminate() override { signal_group(SIGTERM); }

    void write_stdin(std::shared_ptr<const std::string> data, std::function<void(bool)> done) override {
        if (!in_ || queued_ + data->size() > stdin_queue_limit) {
            if (done) asio::post(io_, [done] { done(false); });
            return;
        }
        queued_ += data->size();
        writes_.emplace_back(std::move(data), std::move(done));
        if (writes_.size() == 1) write_next();
    }
    std::size_t stdin_queued_bytes() const override { return queued_; }
    void close_stdin() override {
        if (!writes_.empty()) { close_after_writes_ = true; return; }
        boost::system::error_code ignored;
        if (in_) in_->close(ignored);
        in_.reset();
    }

private:
    asio::io_context& io_;
    Handlers handlers_;
    pid_t pid_ = -1;
    bool running_ = false, close_after_writes_ = false;
    std::optional<asio::posix::stream_descriptor> in_, out_, err_;
    std::deque<std::pair<std::shared_ptr<const std::string>, std::function<void(bool)>>> writes_;
    std::size_t queued_ = 0;

    void signal_group(int signal) {
        if (!running_ || pid_ <= 0) return;
        if (::kill(-pid_, signal) != 0) ::kill(pid_, signal);
    }

    void read(asio::posix::stream_descriptor& stream, bool is_stdout) {
        auto buffer = std::make_shared<std::array<char, 65536>>();
        auto self = std::static_pointer_cast<PosixChild>(shared_from_this());
        stream.async_read_some(asio::buffer(*buffer), [self, buffer, is_stdout](const boost::system::error_code& error, std::size_t n) {
            auto& handler = is_stdout ? self->handlers_.on_stdout : self->handlers_.on_stderr;
            if (n > 0 && handler) handler(std::string_view(buffer->data(), n));
            auto& stream = is_stdout ? self->out_ : self->err_;
            if (error || !stream) {
                auto& end = is_stdout ? self->handlers_.on_stdout_end : self->handlers_.on_stderr_end;
                if (stream) { boost::system::error_code ignored; stream->close(ignored); stream.reset(); }
                if (end) end();
                return;
            }
            self->read(*stream, is_stdout);
        });
    }

    void write_next() {
        if (writes_.empty() || !in_) return;
        auto self = std::static_pointer_cast<PosixChild>(shared_from_this());
        auto data = writes_.front().first;
        asio::async_write(*in_, asio::buffer(*data), [self, data](const boost::system::error_code& error, std::size_t) {
            if (self->writes_.empty()) return;
            auto done = std::move(self->writes_.front().second);
            self->queued_ -= self->writes_.front().first->size();
            self->writes_.pop_front();
            if (done) done(!error);
            if (error) {
                for (auto& [pending, callback] : self->writes_) if (callback) callback(false);
                self->writes_.clear();
                self->queued_ = 0;
                boost::system::error_code ignored;
                if (self->in_) self->in_->close(ignored);
                self->in_.reset();
                return;
            }
            if (self->writes_.empty() && self->close_after_writes_) { self->close_after_writes_ = false; self->close_stdin(); return; }
            self->write_next();
        });
    }

    void exited(int status) {
        running_ = false;
        ExitStatus exit;
        if (WIFSIGNALED(status)) exit.signal = signal_name(WTERMSIG(status));
        else exit.code = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
        if (handlers_.on_exit) handlers_.on_exit(exit);
    }
};
}

std::shared_ptr<ChildProcess> ChildProcess::spawn(boost::asio::io_context& io, const ProcessSpec& spec, Handlers handlers) {
    static const bool sigpipe_ignored = [] { std::signal(SIGPIPE, SIG_IGN); return true; }();
    (void)sigpipe_ignored;
    asio::use_service<ChildWatcher>(io);  // installed before the child can exit
    auto child = std::make_shared<PosixChild>(io, std::move(handlers));
    child->start(spec);
    child->begin();
    return child;
}
}
#endif
