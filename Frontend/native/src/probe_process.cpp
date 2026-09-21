#include "probe_process.hpp"
#include "content_internal.hpp"
#include "planning_internal.hpp"
#include "store_paths.hpp"
#include <cerrno>
#include <cstdlib>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <thread>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
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
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#endif
    read_end.fd = fds[0];
    write_end.fd = fds[1];
}
void nonblocking(int fd) { ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK); }
// Owns the child until reaped: every exit path kills (if still running) and waits.
struct Child {
    pid_t pid = -1;
    bool reaped = false;
    int status = 0;
    ~Child() { if (pid > 0 && !reaped) { ::kill(pid, SIGKILL); wait_blocking(); } }
    void wait_blocking() {
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        reaped = true;
    }
    bool try_wait() {
        pid_t got;
        do got = ::waitpid(pid, &status, WNOHANG); while (got < 0 && errno == EINTR);
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
           std::string_view input, std::chrono::milliseconds timeout, std::size_t output_limit) {
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
    {   // exec success closes the pipe; failure delivers errno.
        int code = 0;
        ssize_t got;
        do got = ::read(fail_r.fd, &code, sizeof code); while (got < 0 && errno == EINTR);
        if (got == static_cast<ssize_t>(sizeof code)) {
            child.wait_blocking();
            os_failure("probe exec", executable, code);
        }
    }
    nonblocking(in_w.fd); nonblocking(out_r.fd); nonblocking(err_r.fd);
    Result result;
    std::size_t written = 0;
    if (input.empty()) in_w.reset();
    auto drain = [&](Fd& fd, std::string& into) {
        char buffer[65536];
        for (;;) {
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
    while (in_w.fd >= 0 || out_r.fd >= 0 || err_r.fd >= 0) {
        pollfd fds[3];
        nfds_t count = 0;
        int in_at = -1, out_at = -1, err_at = -1;
        if (in_w.fd >= 0) { in_at = static_cast<int>(count); fds[count++] = {in_w.fd, POLLOUT, 0}; }
        if (out_r.fd >= 0) { out_at = static_cast<int>(count); fds[count++] = {out_r.fd, POLLIN, 0}; }
        if (err_r.fd >= 0) { err_at = static_cast<int>(count); fds[count++] = {err_r.fd, POLLIN, 0}; }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0) throw Timeout("codec helper timed out");
        const int ready = ::poll(fds, count, static_cast<int>(remaining > 1000 ? 1000 : remaining));
        if (ready < 0) { if (errno == EINTR) continue; os_failure("probe poll", executable, errno); }
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
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!::CreatePipe(&read_end.h, &write_end.h, &attributes, 0)) os_failure("probe pipe", executable, static_cast<int>(::GetLastError()));
    // Only the child's end stays inheritable.
    ::SetHandleInformation(child_reads ? write_end.h : read_end.h, HANDLE_FLAG_INHERIT, 0);
}
}
Result run(const fs::path& executable, const std::vector<std::string>& arguments,
           std::string_view input, std::chrono::milliseconds timeout, std::size_t output_limit) {
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
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
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
    const std::wstring application = executable.wstring();
    if (!::CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, TRUE,
                          CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                          &startup.StartupInfo, &info))
        os_failure("probe spawn", executable, static_cast<int>(::GetLastError()));
    Handle process, thread;
    process.h = info.hProcess;
    thread.h = info.hThread;
    struct Kill { HANDLE p; bool armed = true; ~Kill() { if (armed) { ::TerminateProcess(p, 1); ::WaitForSingleObject(p, INFINITE); } } } kill{process.h};
    if (!::AssignProcessToJobObject(job.h, process.h)) os_failure("probe job", executable, static_cast<int>(::GetLastError()));
    ::ResumeThread(thread.h);
    in_r.reset(); out_w.reset(); err_w.reset();
    Result result;
    bool overflow = false;
    auto reader = [&](HANDLE h, std::string& into) {
        char buffer[65536];
        DWORD got = 0;
        while (::ReadFile(h, buffer, sizeof buffer, &got, nullptr) && got) {
            if (into.size() + got > output_limit) { overflow = true; ::TerminateProcess(process.h, 1); return; }
            into.append(buffer, got);
        }
    };
    // One thread per pipe: no pipe can fill while another is being serviced.
    std::thread out_thread(reader, out_r.h, std::ref(result.out));
    std::thread err_thread(reader, err_r.h, std::ref(result.err));
    std::thread in_thread([&] {
        std::size_t written = 0;
        while (written < input.size()) {
            DWORD put = 0;
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(input.size() - written, 65536));
            if (!::WriteFile(in_w.h, input.data() + written, chunk, &put, nullptr)) break; // broken pipe ignored
            written += put;
        }
        in_w.reset();
    });
    const DWORD waited = ::WaitForSingleObject(process.h, static_cast<DWORD>(timeout.count()));
    const bool timed_out = waited != WAIT_OBJECT_0;
    if (timed_out) { ::TerminateProcess(process.h, 1); ::WaitForSingleObject(process.h, INFINITE); }
    // Termination closes the child's pipe ends, so the threads finish.
    in_thread.join(); out_thread.join(); err_thread.join();
    kill.armed = false;
    if (overflow) throw OutputLimit("codec helper output exceeds native bound");
    if (timed_out) throw Timeout("codec helper timed out");
    DWORD code = 0;
    ::GetExitCodeProcess(process.h, &code);
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
}
