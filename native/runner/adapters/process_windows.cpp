#ifdef _WIN32
#include "process.hpp"
#include <boost/asio/post.hpp>
#include <boost/asio/windows/object_handle.hpp>
#include <boost/asio/windows/stream_handle.hpp>
#include <boost/asio/write.hpp>
#include <windows.h>
#include <array>
#include <atomic>
#include <deque>

namespace odeum::runner {
namespace {
namespace asio = boost::asio;
constexpr std::size_t stdin_queue_limit = 256u * 1024 * 1024;

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) throw SpawnError("spawn EINVAL (argument is not UTF-8)");
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

const char* error_name(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_DIRECTORY: return "ENOENT";
        case ERROR_ACCESS_DENIED: return "EACCES";
        case ERROR_BAD_EXE_FORMAT: return "ENOEXEC";
        case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY: return "ENOMEM";
        default: return "EUNKNOWN";
    }
}

struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = nullptr; }
    Handle& operator=(Handle&& other) noexcept { reset(); value = other.value; other.value = nullptr; return *this; }
    ~Handle() { reset(); }
    void reset() { if (value) CloseHandle(value); value = nullptr; }
    HANDLE release() { auto h = value; value = nullptr; return h; }
};

bool file_exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool has_extension(const std::wstring& name) {
    const auto slash = name.find_last_of(L"\\/");
    const auto dot = name.find_last_of(L'.');
    return dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash);
}

bool absolute_or_relative_path(const std::wstring& name) {
    return name.find_first_of(L"\\/") != std::wstring::npos || (name.size() >= 2 && name[1] == L':');
}

std::wstring join(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    const wchar_t last = dir.back();
    return (last == L'\\' || last == L'/') ? dir + name : dir + L"\\" + name;
}

std::wstring full_path(const std::wstring& path) {
    const DWORD n = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (n == 0) return path;
    std::wstring out(n, L'\0');
    const DWORD m = GetFullPathNameW(path.c_str(), n, out.data(), nullptr);
    out.resize(m);
    return out;
}

// libuv's search_path: the child's working directory first, then PATH; a name without an
// extension also tries ".com" and ".exe".
std::wstring resolve_program(const std::wstring& program, const std::wstring& cwd, const std::wstring& path_env) {
    auto try_dir = [&](const std::wstring& dir) -> std::wstring {
        const auto base = join(dir, program);
        if (has_extension(program) && file_exists(base)) return full_path(base);
        for (const wchar_t* ext : {L".com", L".exe"}) {
            if (file_exists(base + ext)) return full_path(base + ext);
        }
        return {};
    };
    std::wstring cwd_dir = cwd;
    if (cwd_dir.empty()) {
        const DWORD n = GetCurrentDirectoryW(0, nullptr);
        cwd_dir.resize(n);
        cwd_dir.resize(GetCurrentDirectoryW(n, cwd_dir.data()));
    }
    if (absolute_or_relative_path(program)) {
        const bool absolute = program.size() >= 2 && (program[1] == L':' || (program[0] == L'\\' && program[1] == L'\\'));
        if (absolute) {
            if (has_extension(program) && file_exists(program)) return program;
            for (const wchar_t* ext : {L".com", L".exe"}) if (file_exists(program + ext)) return program + ext;
            return {};
        }
        return try_dir(cwd_dir);
    }
    if (auto found = try_dir(cwd_dir); !found.empty()) return found;
    std::size_t start = 0;
    while (start < path_env.size()) {
        auto end = path_env.find(L';', start);
        if (end == std::wstring::npos) end = path_env.size();
        auto dir = path_env.substr(start, end - start);
        if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"') dir = dir.substr(1, dir.size() - 2);
        if (!dir.empty()) if (auto found = try_dir(dir); !found.empty()) return found;
        start = end + 1;
    }
    return {};
}

bool same_name(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// The parent's environment with overrides; names compare case-insensitively as Windows does.
std::vector<std::wstring> merged_environment(const ProcessSpec& spec, std::wstring& path_out) {
    std::vector<std::wstring> env;
    if (auto block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p; p += wcslen(p) + 1) env.emplace_back(p);
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [name, value] : spec.env) {
        const auto wname = widen(name);
        bool replaced = false;
        for (auto& entry : env) {
            const auto eq = entry.find(L'=', 1);
            if (eq != std::wstring::npos && same_name(entry.substr(0, eq), wname)) { entry = wname + L"=" + widen(value); replaced = true; break; }
        }
        if (!replaced) env.push_back(wname + L"=" + widen(value));
    }
    for (const auto& entry : env) {
        const auto eq = entry.find(L'=', 1);
        if (eq != std::wstring::npos && same_name(entry.substr(0, eq), L"PATH")) path_out = entry.substr(eq + 1);
    }
    return env;
}

std::wstring environment_block(const std::vector<std::wstring>& env) {
    std::wstring block;
    for (const auto& entry : env) { block += entry; block.push_back(L'\0'); }
    block.push_back(L'\0');
    return block;
}

std::atomic<unsigned long> pipe_counter{0};

// An overlapped server end for the runner and an inheritable synchronous end for the child.
struct PipePair {
    Handle parent, child;
};

PipePair make_pipe(bool child_reads) {
    wchar_t name[128];
    swprintf(name, 128, L"\\\\.\\pipe\\odeum-runner-%lu-%lu-%llx", GetCurrentProcessId(), ++pipe_counter,
        static_cast<unsigned long long>(GetTickCount64()));
    const DWORD access = (child_reads ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND) | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE;
    Handle parent(CreateNamedPipeW(name, access, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 65536, 65536, 0, nullptr));
    if (!parent.value) throw SpawnError(std::string("pipe ") + error_name(GetLastError()));
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle child(CreateFileW(name, child_reads ? GENERIC_READ : GENERIC_WRITE, 0, &inherit, OPEN_EXISTING, 0, nullptr));
    if (!child.value) throw SpawnError(std::string("pipe ") + error_name(GetLastError()));
    return {std::move(parent), std::move(child)};
}

Handle null_device() {
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle h(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr));
    if (!h.value) throw SpawnError(std::string("open NUL ") + error_name(GetLastError()));
    return h;
}

class WindowsChild final : public ChildProcess {
public:
    WindowsChild(asio::io_context& io, Handlers handlers) : io_(io), handlers_(std::move(handlers)), exit_wait_(io) {}
    ~WindowsChild() override {
        boost::system::error_code ignored;
        if (in_) in_->close(ignored);
        if (out_) out_->close(ignored);
        if (err_) err_->close(ignored);
    }

    void start(const ProcessSpec& spec) {
        std::wstring path_env;
        const auto env = merged_environment(spec, path_env);
        const auto wcwd = widen(spec.cwd);
        const auto program = resolve_program(widen(spec.program), wcwd, path_env);
        if (program.empty()) throw SpawnError("spawn " + spec.program + " ENOENT");
        // Batch files would run through cmd.exe with its own quoting; Node refuses them without a shell.
        if (is_windows_batch_file(narrow(program))) throw SpawnError("spawn " + spec.program + " EINVAL");

        std::optional<PipePair> in, out, err;
        Handle null_handle = null_device();
        if (spec.stdin_pipe) in = make_pipe(true);
        if (spec.stdout_pipe) out = make_pipe(false);
        if (spec.stderr_pipe) err = make_pipe(false);
        HANDLE child_in = in ? in->child.value : null_handle.value;
        HANDLE child_out = out ? out->child.value : null_handle.value;
        HANDLE child_err = err ? err->child.value : null_handle.value;

        std::wstring command = widen(quote_windows_argument(narrow(program)));
        for (const auto& a : spec.args) command += L" " + widen(quote_windows_argument(a));
        auto block = environment_block(env);

        // Only the three standard handles are inherited, never the runner's sockets or files.
        std::vector<HANDLE> inherited{child_in};
        if (child_out != child_in) inherited.push_back(child_out);
        if (child_err != child_in && child_err != child_out) inherited.push_back(child_err);
        SIZE_T list_size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
        std::vector<unsigned char> list_storage(list_size);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list_storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &list_size)) throw SpawnError("spawn EUNKNOWN");
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(), inherited.size() * sizeof(HANDLE), nullptr, nullptr);

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = child_in;
        startup.StartupInfo.hStdOutput = child_out;
        startup.StartupInfo.hStdError = child_err;
        if (spec.hide_window) { startup.StartupInfo.dwFlags |= STARTF_USESHOWWINDOW; startup.StartupInfo.wShowWindow = SW_HIDE; }
        startup.lpAttributeList = attributes;

        job_ = Handle(CreateJobObjectW(nullptr, nullptr));
        if (!job_.value) { DeleteProcThreadAttributeList(attributes); throw SpawnError("spawn EUNKNOWN (job)"); }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job_.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

        PROCESS_INFORMATION info{};
        const DWORD flags = CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | (spec.hide_window ? CREATE_NO_WINDOW : 0);
        const BOOL created = CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE, flags, block.data(),
            wcwd.empty() ? nullptr : wcwd.c_str(), &startup.StartupInfo, &info);
        const DWORD error = GetLastError();
        DeleteProcThreadAttributeList(attributes);
        if (!created) throw SpawnError("spawn " + spec.program + " " + error_name(error));
        Handle thread(info.hThread);
        process_ = Handle(info.hProcess);
        if (!AssignProcessToJobObject(job_.value, process_.value)) {
            TerminateProcess(process_.value, 1);
            throw SpawnError("spawn " + spec.program + " EUNKNOWN (job assignment)");
        }
        ResumeThread(thread.value);
        pid_ = static_cast<long long>(info.dwProcessId);
        running_ = true;
        if (in) in_.emplace(io_, in->parent.release());
        if (out) out_.emplace(io_, out->parent.release());
        if (err) err_.emplace(io_, err->parent.release());
        HANDLE duplicate = nullptr;
        DuplicateHandle(GetCurrentProcess(), process_.value, GetCurrentProcess(), &duplicate, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0);
        exit_wait_.assign(duplicate);
    }

    void begin() {
        auto self = std::static_pointer_cast<WindowsChild>(shared_from_this());
        if (out_) read(true);
        if (err_) read(false);
        exit_wait_.async_wait([self](const boost::system::error_code& error) {
            if (error) return;
            self->exited();
        });
    }

    long long pid() const override { return pid_; }
    bool running() const override { return running_; }
    void kill_tree() override { stop("SIGKILL"); }
    void terminate() override { stop("SIGTERM"); }

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
    Handle job_, process_;
    asio::windows::object_handle exit_wait_;
    long long pid_ = 0;
    bool running_ = false, close_after_writes_ = false;
    std::optional<std::string> stop_signal_;
    std::optional<asio::windows::stream_handle> in_, out_, err_;
    std::deque<std::pair<std::shared_ptr<const std::string>, std::function<void(bool)>>> writes_;
    std::size_t queued_ = 0;

    void stop(const char* signal) {
        if (!running_ || !job_.value) return;
        // As libuv reports a TerminateProcess kill: the signal that was asked for.
        if (!stop_signal_) stop_signal_ = signal;
        TerminateJobObject(job_.value, 1);
    }

    void read(bool is_stdout) {
        auto buffer = std::make_shared<std::array<char, 65536>>();
        auto self = std::static_pointer_cast<WindowsChild>(shared_from_this());
        auto& stream = is_stdout ? out_ : err_;
        stream->async_read_some(asio::buffer(*buffer), [self, buffer, is_stdout](const boost::system::error_code& error, std::size_t n) {
            auto& handler = is_stdout ? self->handlers_.on_stdout : self->handlers_.on_stderr;
            if (n > 0 && handler) handler(std::string_view(buffer->data(), n));
            auto& stream = is_stdout ? self->out_ : self->err_;
            if (error || !stream) {
                auto& end = is_stdout ? self->handlers_.on_stdout_end : self->handlers_.on_stderr_end;
                if (stream) { boost::system::error_code ignored; stream->close(ignored); stream.reset(); }
                if (end) end();
                return;
            }
            self->read(is_stdout);
        });
    }

    void write_next() {
        if (writes_.empty() || !in_) return;
        auto self = std::static_pointer_cast<WindowsChild>(shared_from_this());
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

    void exited() {
        running_ = false;
        DWORD code = 0;
        GetExitCodeProcess(process_.value, &code);
        ExitStatus exit;
        if (stop_signal_) exit.signal = *stop_signal_;
        else exit.code = static_cast<int>(code);
        if (handlers_.on_exit) handlers_.on_exit(exit);
    }
};
}

std::shared_ptr<ChildProcess> ChildProcess::spawn(boost::asio::io_context& io, const ProcessSpec& spec, Handlers handlers) {
    auto child = std::make_shared<WindowsChild>(io, std::move(handlers));
    child->start(spec);
    child->begin();
    return child;
}
}
#endif
