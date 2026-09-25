#include "probe_process.hpp"
#include "c2/frontend/core.hpp"
#include "content_internal.hpp"
#include "schema_compat.hpp"
#include "planning_internal.hpp"
#include "store_paths.hpp"
#include "store_write.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <atomic>
#include <memory>
#include <thread>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <unistd.h>
#endif
namespace fs = std::filesystem;
namespace c2::frontend::probe_process {
namespace {
using Clock = std::chrono::steady_clock;
[[noreturn]] void os_failure(const char* what, const fs::path& path, int code) {
#ifdef _WIN32
    throw fs::filesystem_error(what, path, std::error_code(code, std::system_category()));
#else
    throw fs::filesystem_error(what, path, std::error_code(code, std::generic_category()));
#endif
}
#ifndef _WIN32
struct Fd {
    int fd = -1;
    Fd() = default;
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    ~Fd() { reset(); }
    void reset() { if (fd >= 0) { ::close(fd); fd = -1; } }
};
void make_pipe(Fd& read_end, Fd& write_end, const fs::path& executable) {
    int fds[2];
#if defined(__linux__)
    if (::pipe2(fds, O_CLOEXEC) != 0) os_failure("probe pipe", executable, errno);
#else
    if (::pipe(fds) != 0) os_failure("probe pipe", executable, errno);

#endif
    read_end.fd = fds[0];
    write_end.fd = fds[1];
    // Reserve 0..2 for dup2 even when the caller started with closed stdio.
    for (Fd* end : {&read_end, &write_end}) {
        if (end->fd < 3) {
            const int moved = ::fcntl(end->fd, F_DUPFD_CLOEXEC, 3);
            if (moved < 0) os_failure("probe pipe descriptor", executable, errno);
            end->reset();
            end->fd = moved;
        } else if (::fcntl(end->fd, F_SETFD, FD_CLOEXEC) < 0) {
            os_failure("probe pipe flags", executable, errno);
        }
    }
}
void nonblocking(int fd, const fs::path& executable) {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        os_failure("probe pipe flags", executable, errno);
}
// A cleanup waiter is allocated and started BEFORE fork, so thread startup
// failure cannot strand a child. Normal operation alone owns waitpid/kill.
// On failure it sends SIGKILL, then transfers the owned PID to this waiter;
// a kernel-delayed exit cannot make the caller's deadline an unbounded reap.
// No reference to the runner's stack escapes and no thread is ever joined.
struct Child {
    std::shared_ptr<std::atomic<pid_t>> cleanup = std::make_shared<std::atomic<pid_t>>(-2);
    pid_t pid = -1;
    bool reaped = false;
    int status = 0;
    Child() {
        std::thread([state = cleanup] {
            pid_t owned;
            while ((owned = state->load()) == -2) ::usleep(1000);
            if (owned > 0) {
                int ignored;
                while (::waitpid(owned, &ignored, 0) < 0 && errno == EINTR) {}
            }
        }).detach();
    }
    ~Child() {
        if (pid > 0 && !reaped) {
            ::kill(pid, SIGKILL);
            // Usually reap immediately; otherwise the already-running waiter
            // retains exclusive ownership until the child actually exits.
            const auto reap_deadline = Clock::now() + std::chrono::milliseconds(250);
            do {
                const pid_t got = ::waitpid(pid, &status, WNOHANG);
                if (got == pid || (got < 0 && errno == ECHILD)) { reaped = true; break; }
                ::usleep(1000);
            } while (Clock::now() < reap_deadline);
        }
        cleanup->store(pid > 0 && !reaped ? pid : -1);
    }
    bool try_wait() {
        const pid_t got = ::waitpid(pid, &status, WNOHANG);
        if (got < 0 && errno != EINTR) {
            const int code = errno;
            if (code == ECHILD) reaped = true; // never signal a no-longer-owned PID
            os_failure("probe wait", {}, code);
        }
        if (got == pid) reaped = true;
        return reaped;
    }
};
// A helper that exits without reading stdin must produce EPIPE, not kill this
// process: block SIGPIPE for the calling thread and discard one raised here.
struct SigpipeGuard {
    sigset_t previous{};
    bool was_pending = false;
    SigpipeGuard() {
        sigset_t pending;
        ::sigpending(&pending);
        was_pending = ::sigismember(&pending, SIGPIPE) == 1;
        sigset_t block;
        ::sigemptyset(&block);
        ::sigaddset(&block, SIGPIPE);
        ::pthread_sigmask(SIG_BLOCK, &block, &previous);
    }
    ~SigpipeGuard() {
        if (!was_pending) {
            sigset_t pending;
            ::sigpending(&pending);
            if (::sigismember(&pending, SIGPIPE) == 1) {
                sigset_t only;
                ::sigemptyset(&only);
                ::sigaddset(&only, SIGPIPE);
#if defined(__linux__)
                const timespec zero{0, 0};
                ::sigtimedwait(&only, nullptr, &zero);
#else
                int ignored;
                ::sigwait(&only, &ignored);
#endif
            }
        }
        ::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    }
};
#endif
} // namespace

#ifndef _WIN32
Result run(const fs::path& executable, const std::vector<std::string>& arguments,
           std::string_view input, std::chrono::milliseconds timeout, std::size_t output_limit, const FailureHook& hook) {
    const auto deadline = Clock::now() + timeout;
    Fd in_r, in_w, out_r, out_w, err_r, err_w, fail_r, fail_w;
    make_pipe(in_r, in_w, executable);
    make_pipe(out_r, out_w, executable);
    make_pipe(err_r, err_w, executable);
    make_pipe(fail_r, fail_w, executable);
    // Everything the child needs is built before fork; only async-signal-safe
    // calls run between fork and exec.
    const std::string program = executable.string();
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& a : arguments) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    // Never approximate the descriptor range with RLIMIT_NOFILE: the caller
    // can lower even its hard limit while higher inherited descriptors stay
    // open. Unsupported backends must fail closed instead of leaking them.
#if !defined(__linux__) || !defined(SYS_close_range)
    os_failure("probe requires close_range descriptor isolation", executable, ENOTSUP);
#endif
    SigpipeGuard sigpipe;
    Child child;
    child.pid = ::fork();
    if (child.pid < 0) { child.pid = -1; os_failure("probe fork", executable, errno); }
    if (child.pid == 0) {
        // dup2 clears CLOEXEC on the standard descriptors; all others close on exec.
        if (::dup2(in_r.fd, 0) < 0 || ::dup2(out_w.fd, 1) < 0 || ::dup2(err_w.fd, 2) < 0) {
            const int code = errno;
            (void)!::write(fail_w.fd, &code, sizeof code);
            ::_exit(127);
        }
        // Keep only the exec-error pipe above stdio. All pipe descriptors were
        // moved above 2, so none of the dup2 sources can alias a destination.
#if defined(__linux__) && defined(SYS_close_range)
        const long low = fail_w.fd == 3 ? 0 : ::syscall(SYS_close_range, 3u, static_cast<unsigned>(fail_w.fd - 1), 0u);
        const int low_error = errno;
        const long high = ::syscall(SYS_close_range, static_cast<unsigned>(fail_w.fd + 1), ~0u, 0u);
        if (low != 0 || high != 0) {
            const int code = low != 0 ? low_error : errno;
            (void)!::write(fail_w.fd, &code, sizeof code);
            ::_exit(127);
        }
#endif
        sigset_t none;
        ::sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        ::signal(SIGPIPE, SIG_DFL);
        ::execv(program.c_str(), argv.data());
        const int code = errno;
        (void)!::write(fail_w.fd, &code, sizeof code);
        ::_exit(127);
    }
    in_r.reset(); out_w.reset(); err_w.reset(); fail_w.reset();
    nonblocking(in_w.fd, executable); nonblocking(out_r.fd, executable);
    nonblocking(err_r.fd, executable); nonblocking(fail_r.fd, executable);
    if (hook) hook(); // test-only failure after spawn and descriptor setup
    std::string exec_error;
    Result result;
    std::size_t written = 0;
    if (input.empty()) in_w.reset();
    auto drain = [&](Fd& fd, std::string& into) {
        char buffer[65536];
        for (int reads = 0; reads < 4; ++reads) {
            if (Clock::now() >= deadline) throw Timeout("codec helper timed out");
            const ssize_t got = ::read(fd.fd, buffer, sizeof buffer);
            if (got > 0) {
                if (into.size() + static_cast<std::size_t>(got) > output_limit)
                    throw OutputLimit("codec helper output exceeds native bound");
                into.append(buffer, static_cast<std::size_t>(got));
                continue;
            }
            if (got == 0) { fd.reset(); return; }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            os_failure("probe read", executable, errno);
        }
    };
    // All three pipes are serviced together so neither side can block on a
    // full pipe (the classic write-all-then-read deadlock).
    while (in_w.fd >= 0 || out_r.fd >= 0 || err_r.fd >= 0 || fail_r.fd >= 0) {
        pollfd fds[4];
        nfds_t count = 0;
        int in_at = -1, out_at = -1, err_at = -1, fail_at = -1;
        if (in_w.fd >= 0) { in_at = static_cast<int>(count); fds[count++] = {in_w.fd, POLLOUT, 0}; }
        if (out_r.fd >= 0) { out_at = static_cast<int>(count); fds[count++] = {out_r.fd, POLLIN, 0}; }
        if (err_r.fd >= 0) { err_at = static_cast<int>(count); fds[count++] = {err_r.fd, POLLIN, 0}; }
        if (fail_r.fd >= 0) { fail_at = static_cast<int>(count); fds[count++] = {fail_r.fd, POLLIN, 0}; }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0) throw Timeout("codec helper timed out");
        const int ready = ::poll(fds, count, static_cast<int>(remaining > 1000 ? 1000 : remaining));
        if (ready < 0) { if (errno == EINTR) continue; os_failure("probe poll", executable, errno); }
        if (fail_at >= 0 && fds[fail_at].revents) {
            char bytes[sizeof(int)];
            const ssize_t got = ::read(fail_r.fd, bytes, sizeof bytes);
            if (got > 0) exec_error.append(bytes, static_cast<std::size_t>(got));
            else if (got == 0) fail_r.reset();
            else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                os_failure("probe exec handshake", executable, errno);
            if (exec_error.size() == sizeof(int)) {
                int code;
                std::memcpy(&code, exec_error.data(), sizeof code);
                os_failure("probe exec", executable, code);
            }
            if (fail_r.fd < 0 && !exec_error.empty()) os_failure("probe exec handshake", executable, EIO);
        }
        if (in_at >= 0 && fds[in_at].revents) {
            const ssize_t put = ::write(in_w.fd, input.data() + written, input.size() - written);
            if (put > 0) { written += static_cast<std::size_t>(put); if (written == input.size()) in_w.reset(); }
            else if (put < 0 && errno == EPIPE) in_w.reset(); // communicate() ignores BrokenPipeError
            else if (put < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) os_failure("probe write", executable, errno);
        }
        if (out_at >= 0 && fds[out_at].revents) drain(out_r, result.out);
        if (err_at >= 0 && fds[err_at].revents) drain(err_r, result.err);
    }
    // Streams are closed; the child may still run until the deadline.
    while (!child.try_wait()) {
        if (Clock::now() >= deadline) throw Timeout("codec helper timed out");
        ::usleep(2000);
    }
    if (WIFEXITED(child.status)) result.returncode = WEXITSTATUS(child.status);
    else if (WIFSIGNALED(child.status)) result.returncode = -WTERMSIG(child.status);
    else result.returncode = child.status;
    return result;
}
#else
namespace {
struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    void reset() { if (h && h != INVALID_HANDLE_VALUE) ::CloseHandle(h); h = nullptr; }
};
// subprocess.list2cmdline quoting (MS C runtime argument rules).
void append_argument(std::wstring& line, const std::wstring& argument) {
    if (!line.empty()) line.push_back(L' ');
    const bool quote = argument.empty() || argument.find_first_of(L" \t") != std::wstring::npos;
    if (quote) line.push_back(L'"');
    std::size_t slashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { line.append(slashes * 2 + 1, L'\\'); line.push_back(L'"'); slashes = 0; continue; }
        line.append(slashes, L'\\'); slashes = 0; line.push_back(c);
    }
    line.append(quote ? slashes * 2 : slashes, L'\\');
    if (quote) line.push_back(L'"');
}
std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) throw std::invalid_argument("probe argument is not UTF-8");
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), wide.data(), n);
    return wide;
}
void make_pipe(Handle& read_end, Handle& write_end, bool child_reads, const fs::path& executable) {
    // Parent ends are synchronous, nonblocking byte-mode named pipes. There
    // are no worker threads, pending OVERLAPPED buffers, cancellation races,
    // or joins. Children receive ordinary blocking handles. Every operation
    // in the owner loop returns immediately (PIPE_NOWAIT).
    const std::string id = store_write::new_id();
    const std::wstring name = L"\\\\.\\pipe\\c2-codec-" + std::wstring(id.begin(), id.end());
    Handle& parent = child_reads ? write_end : read_end;
    Handle& child = child_reads ? read_end : write_end;
    parent.h = ::CreateNamedPipeW(name.c_str(),
        (child_reads ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND) | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 65536, 65536, 0, nullptr);
    if (parent.h == INVALID_HANDLE_VALUE) os_failure("probe pipe", executable, static_cast<int>(::GetLastError()));
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    child.h = ::CreateFileW(name.c_str(), child_reads ? GENERIC_READ : GENERIC_WRITE,
        0, &attributes, OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
    if (child.h == INVALID_HANDLE_VALUE) os_failure("probe pipe client", executable, static_cast<int>(::GetLastError()));
    if (!::ConnectNamedPipe(parent.h, nullptr)) {
        const DWORD code = ::GetLastError();
        if (code != ERROR_PIPE_CONNECTED) os_failure("probe pipe connect", executable, static_cast<int>(code));
    }
}
}
Result run(const fs::path& executable, const std::vector<std::string>& arguments,
           std::string_view input, std::chrono::milliseconds timeout, std::size_t output_limit, const FailureHook& hook) {
    const auto deadline = Clock::now() + timeout;
    Handle in_r, in_w, out_r, out_w, err_r, err_w;
    make_pipe(in_r, in_w, true, executable);
    make_pipe(out_r, out_w, false, executable);
    make_pipe(err_r, err_w, false, executable);
    std::wstring line;
    append_argument(line, executable.wstring());
    for (const auto& a : arguments) append_argument(line, widen(a));
    // Restrict inheritance to exactly the three child ends.
    HANDLE inherited[3] = {in_r.h, out_w.h, err_w.h};
    SIZE_T size = 0;
    if (::InitializeProcThreadAttributeList(nullptr, 1, 0, &size) || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        os_failure("probe spawn attributes", executable, static_cast<int>(::GetLastError()));
    std::vector<char> storage(size);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!::InitializeProcThreadAttributeList(list, 1, 0, &size)) os_failure("probe spawn", executable, static_cast<int>(::GetLastError()));
    struct ListGuard { LPPROC_THREAD_ATTRIBUTE_LIST l; ~ListGuard() { ::DeleteProcThreadAttributeList(l); } } list_guard{list};
    if (!::UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited, nullptr, nullptr))
        os_failure("probe spawn", executable, static_cast<int>(::GetLastError()));
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = in_r.h;
    startup.StartupInfo.hStdOutput = out_w.h;
    startup.StartupInfo.hStdError = err_w.h;
    startup.lpAttributeList = list;
    // The job kills the child if this process dies or any path leaves early.
    Handle job;
    job.h = ::CreateJobObjectW(nullptr, nullptr);
    if (!job.h) os_failure("probe job", executable, static_cast<int>(::GetLastError()));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof limits))
        os_failure("probe job", executable, static_cast<int>(::GetLastError()));
    PROCESS_INFORMATION info{};
    const std::wstring application = fs::absolute(executable).wstring();
    if (!::CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, TRUE,
                          CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                          &startup.StartupInfo, &info))
        os_failure("probe spawn", executable, static_cast<int>(::GetLastError()));
    Handle process, thread;
    process.h = info.hProcess;
    thread.h = info.hThread;
    // Before job assignment only the suspended direct child exists. After
    // assignment close/terminate the job *before* releasing pipe ownership,
    // so descendants cannot keep pipes open past the operation deadline.
    struct Kill {
        HANDLE process, job;
        bool assigned = false, armed = true;
        ~Kill() {
            if (armed) {
                if (assigned) ::TerminateJobObject(job, 1);
                else ::TerminateProcess(process, 1);
            }
        }
    } kill{process.h, job.h};
    if (!::AssignProcessToJobObject(job.h, process.h)) os_failure("probe job", executable, static_cast<int>(::GetLastError()));
    kill.assigned = true;
    if (::ResumeThread(thread.h) == static_cast<DWORD>(-1)) os_failure("probe resume", executable, static_cast<int>(::GetLastError()));
    in_r.reset(); out_w.reset(); err_w.reset();
    if (hook) hook(); // test-only failure after job assignment and resume
    Result result;
    std::size_t written = 0;
    if (input.empty()) in_w.reset();
    bool exited = false;
    auto drain = [&](Handle& handle, std::string& into) {
        if (!handle.h) return false;
        char bytes[65536];
        DWORD got = 0;
        if (!::ReadFile(handle.h, bytes, sizeof bytes, &got, nullptr)) {
            const DWORD code = ::GetLastError();
            if (code == ERROR_BROKEN_PIPE) { handle.reset(); return true; }
            if (code == ERROR_NO_DATA) return false;
            os_failure("probe read", executable, static_cast<int>(code));
        }
        if (got > output_limit - into.size()) throw OutputLimit("codec helper output exceeds native bound");
        into.append(bytes, got);
        return got != 0;
    };
    while (!exited || in_w.h || out_r.h || err_r.h) {
        if (Clock::now() >= deadline) throw Timeout("codec helper timed out");
        bool progress = false;
        if (!exited) {
            const DWORD waited = ::WaitForSingleObject(process.h, 0);
            if (waited == WAIT_OBJECT_0) { exited = true; progress = true; }
            else if (waited != WAIT_TIMEOUT) os_failure("probe wait", executable, static_cast<int>(::GetLastError()));
        }
        if (in_w.h) {
            DWORD put = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(65536, input.size() - written));
            if (!::WriteFile(in_w.h, input.data() + written, chunk, &put, nullptr)) {
                const DWORD code = ::GetLastError();
                if (code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA) in_w.reset();
                else os_failure("probe write", executable, static_cast<int>(code));
            } else {
                written += put;
                progress = put != 0;
                if (written == input.size()) in_w.reset();
            }
        }
        progress = drain(out_r, result.out) || progress;
        progress = drain(err_r, result.err) || progress;
        if (!progress) ::Sleep(1);
    }
    DWORD code = 0;
    if (!::GetExitCodeProcess(process.h, &code)) os_failure("probe exit code", executable, static_cast<int>(::GetLastError()));
    if (!::TerminateJobObject(job.h, 1)) os_failure("probe job cleanup", executable, static_cast<int>(::GetLastError()));
    kill.armed = false;
    result.returncode = static_cast<int>(code);
    return result;
}
#endif

compat::Value executable_evidence(const fs::path& path) {
    // resolve(strict=True): a missing path raises FileNotFoundError (OSError).
    const fs::path expanded = store_paths::expand_user(path);
    std::error_code error;
    if (!fs::exists(expanded, error))
        os_failure("executable evidence", expanded, error ? error.value() : static_cast<int>(std::errc::no_such_file_or_directory));
    const fs::path resolved = store_paths::resolve_native(expanded);
    if (!fs::is_regular_file(resolved)) throw ProbeError("explicit executable must be a regular file");
    auto value = planning_internal::object_value();
    value.object.emplace_back(U"path", planning_internal::string_value(store_paths::native_points(resolved)));
    value.object.emplace_back(U"sha256", planning_internal::ascii_value(content_internal::hash_file(resolved)));
    return value;
}

namespace {
std::optional<fs::path> configured(const std::optional<fs::path>& probe) {
    if (probe && !probe->empty()) return probe;
    if (const char* env = std::getenv("C2_PROFILE_PROBE"); env && *env) return fs::path(env);
    return std::nullopt;
}
int hex_digit(char32_t c) {
    if (c >= U'0' && c <= U'9') return static_cast<int>(c - U'0');
    if (c >= U'a' && c <= U'f') return static_cast<int>(c - U'a') + 10;
    if (c >= U'A' && c <= U'F') return static_cast<int>(c - U'A') + 10;
    return -1;
}
}

compat::Value codec_evidence(const std::optional<fs::path>& probe) {
    const auto path = configured(probe);
    if (!path) throw ProbeError("session requires an explicit profile codec helper");
    return executable_evidence(*path);
}

compat::Value codec_inspect(std::string_view content, std::string_view kind,
                            const std::optional<fs::path>& probe, std::string_view dialect,
                            std::chrono::milliseconds timeout) {
    using namespace planning_internal;
    auto fixed = [](std::string_view layout, bool helper_missing) {
        auto value = object_value();
        value.object.emplace_back(U"layout", ascii_value(layout));
        value.object.emplace_back(U"codec_roundtrip_exact", boolean_value(false));
        if (helper_missing) value.object.emplace_back(U"diagnostic", ascii_value("codec-helper-unavailable"));
        return value;
    };
    if (dialect == "iceage-triassic") return fixed("unsupported-iceage-family", false);
    if (content.size() != (kind == "sav" ? 1660u : 7176u)) return fixed("unknown", false);
    const auto path = configured(probe);
    if (!path) return fixed("size-candidate-only", true);
    const Result process = run(*path, {kind == "sav" ? "save" : "room"}, content, timeout);
    if (process.returncode != 0)
        throw ProbeError("codec helper failed (" + std::to_string(process.returncode) + ")");
    // json.loads failure is an unexpected reference ValueError: compat::Error propagates.
    compat::Value result = compat::parse(process.out);
    if (result.kind != compat::Kind::object || !result.contains(U"name_hex")) return result;
    const compat::Value& hex = result.at(U"name_hex");
    if (hex.kind != compat::Kind::string) throw std::invalid_argument("name_hex must be str");
    // bytes.fromhex: ASCII whitespace permitted between byte pairs only.
    std::u32string display;
    bool terminated = false;
    for (std::size_t i = 0; i < hex.string.size();) {
        const char32_t c = hex.string[i];
        if (c == U' ' || (c >= U'\t' && c <= U'\r')) { ++i; continue; }
        const int high = hex_digit(c);
        const int low = i + 1 < hex.string.size() ? hex_digit(hex.string[i + 1]) : -1;
        if (high < 0 || low < 0) throw std::invalid_argument("non-hexadecimal number found in fromhex() arg");
        const auto byte = static_cast<char32_t>(high * 16 + low);
        if (byte == 0) terminated = true;
        if (!terminated) display.push_back(byte); // split(b'\0', 1)[0].decode('latin1')
        i += 2;
    }
    for (auto& member : result.object)
        if (member.first == U"name_display_latin1") { member.second = string_value(std::move(display)); return result; }
    result.object.emplace_back(U"name_display_latin1", string_value(std::move(display)));
    return result;
}

namespace {
std::string narrow_ascii(const std::u32string& text) {
    std::string out;
    for (const char32_t c : text) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    return out;
}
compat::Value* member(compat::Value& object, std::u32string_view key) {
    for (auto& m : object.object) if (m.first == key) return &m.second;
    return nullptr;
}
void assign(compat::Value& object, std::u32string_view key, compat::Value value) {
    if (auto* existing = member(object, key)) { *existing = std::move(value); return; }
    object.object.emplace_back(std::u32string(key), std::move(value));
}
// decoded.get(key, fallback) over a helper result of any kind; a non-object
// has no .get in the reference (AttributeError).
const compat::Value* lookup(const compat::Value& decoded, std::u32string_view key) {
    if (decoded.kind != compat::Kind::object) throw std::invalid_argument("codec helper result is not an object");
    return decoded.contains(key) ? &decoded.at(key) : nullptr;
}
}

compat::Value inspect_set(const ProfileState& state, const std::optional<fs::path>& probe,
                          std::string_view dialect, std::chrono::milliseconds timeout) {
    using namespace planning_internal;
    const auto blobs = state.stable_read();
    compat::Value result = compat::parse(state.export_json());
    const compat::Value entries = result.at(U"files");
    const compat::Value slot = integer_value(state.filename_slot());
    assign(result, U"files", array_value());
    compat::Value files = array_value();
    compat::Value& diagnostics = *member(result, U"diagnostics");
    for (const compat::Value& entry : entries.array) {
        const std::u32string& path = entry.at(U"path").string;
        const std::string* content = nullptr;
        for (const auto& blob : blobs) if (blob.path == path) content = &blob.bytes;
        if (!content) throw std::logic_error("stable read omitted an inventoried member");
        const compat::Value decoded = codec_inspect(*content, narrow_ascii(entry.at(U"kind").string), probe, dialect, timeout);
        compat::Value file = entry;
        assign(file, U"size", integer_value(std::to_string(content->size())));
        assign(file, U"sha256", ascii_value(sha256(*content)));
        assign(file, U"decoded", decoded);
        files.array.push_back(std::move(file));
        const compat::Value* registration = lookup(decoded, U"registration");
        if (registration && !schema::equal(*registration, slot))
            diagnostics.array.push_back(diagnostic_value(U"registration-mismatch", U"Filename slot disagrees with embedded registration; no normalization performed."));
        const compat::Value* exact = lookup(decoded, U"codec_roundtrip_exact");
        if (!exact || !schema::truth(*exact)) {
            auto d = diagnostic_value(U"unreadable-layout", U"Preserved as opaque bytes; no save compatibility claim.");
            d.object.emplace_back(U"path", string_value(path));
            diagnostics.array.push_back(std::move(d));
        }
    }
    diagnostics.array.push_back(diagnostic_value(U"pair-coherence-unverified", U"Two equal observations cannot certify an externally updated SAV/SAB transaction."));
    *member(result, U"files") = std::move(files);
    return result;
}

compat::Value inspect_bytes(const std::vector<CapturedBlob>& blobs, int slot, const fs::path& probe,
                            std::chrono::milliseconds timeout) {
    using namespace planning_internal;
    if (slot < 0 || slot > 7) throw std::invalid_argument("native slot outside 0..7");
    const std::u32string stem = U"trophy0" + std::u32string(1, static_cast<char32_t>(U'0' + slot));
    const compat::Value expected = integer_value(std::to_string(slot));
    compat::Value decoded = object_value(), diagnostics = array_value();
    for (const auto& blob : blobs) {
        const bool save = blob.path == stem + U".sav";
        if (!save && blob.path != stem + U".sab") continue;
        compat::Value value = codec_inspect(blob.bytes, save ? "sav" : "sab", probe, "unknown", timeout);
        auto diagnostic = [&](std::string_view code) {
            auto d = object_value();
            d.object.emplace_back(U"code", ascii_value(code));
            d.object.emplace_back(U"path", string_value(blob.path));
            diagnostics.array.push_back(std::move(d));
        };
        const compat::Value* exact = lookup(value, U"codec_roundtrip_exact");
        if (!exact || !schema::truth(*exact)) diagnostic("unreadable-state");
        const compat::Value* registration = lookup(value, U"registration");
        if (registration && !schema::equal(*registration, expected)) diagnostic("registration-mismatch");
        assign(decoded, blob.path, std::move(value));
    }
    compat::Value result = object_value();
    result.object.emplace_back(U"decoded", std::move(decoded));
    result.object.emplace_back(U"diagnostics", std::move(diagnostics));
    return result;
}
}
