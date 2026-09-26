// Workstream C: owned engine/fixture child supervisor. See session_process.hpp.
// Reference: lodge.session_runner run_session (process part), BoundedLog and
// stop_owned. The reference drains each pipe on a thread; this supervisor
// services both pipes and the child from one loop with the probe runner's
// nonblocking primitives, so the child can never block on a full pipe.
#include "session_process.hpp"
#include "process_internal.hpp"
#include "planning_internal.hpp"
#include "store_paths.hpp"
#include "store_write.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <poll.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#endif
namespace fs = std::filesystem;
namespace c2::frontend::session_process {
using process_internal::Clock;
using process_internal::os_failure;
namespace {
constexpr const char* SUBJECT = "session";
constexpr auto GRACE = std::chrono::milliseconds(500);  // stop_owned wait(timeout=0.5)
constexpr auto REAP = std::chrono::seconds(5);           // process.wait(timeout=5)
constexpr auto FINISH = std::chrono::seconds(2);         // BoundedLog.finish join(timeout=2)
constexpr auto TICK = std::chrono::milliseconds(50);     // process.wait(timeout=0.05)
// Text bytes (strerror) as code points; invalid sequences become U+FFFD.
std::u32string points(std::string_view text) {
    std::u32string out;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        char32_t value = n == 1 ? c : n == 2 ? c & 31 : n == 3 ? c & 15 : c & 7;
        bool valid = n != 0 && i + n <= text.size();
        for (std::size_t k = 1; valid && k < n; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xc0) != 0x80) valid = false; else value = (value << 6) | (next & 63);
        }
        if (!valid) { out.push_back(0xfffd); ++i; continue; }
        out.push_back(value);
        i += n;
    }
    return out;
}
// str(OSError) with a filename: "[Errno N] strerror: repr(path)". repr uses
// single quotes unless the text has one and no double quote, escapes the
// backslash and quote, \n \r \t, and other controls as \xNN (best effort).
std::u32string os_error_text(int code, const fs::path& path) {
    const std::u32string name = store_paths::native_points(path);
    const bool single = name.find(U'\'') == std::u32string::npos || name.find(U'"') != std::u32string::npos;
    const char32_t quote = single ? U'\'' : U'"';
    std::u32string text = points("[Errno " + std::to_string(code) + "] " + std::strerror(code) + ": ");
    text.push_back(quote);
    for (const char32_t c : name) {
        if (c == U'\\' || c == quote) { text.push_back(U'\\'); text.push_back(c); }
        else if (c == U'\n') text += U"\\n";
        else if (c == U'\r') text += U"\\r";
        else if (c == U'\t') text += U"\\t";
        else if (c < 0x20 || c == 0x7f) {
            constexpr char32_t digits[] = U"0123456789abcdef";
            text += U"\\x"; text.push_back(digits[c >> 4]); text.push_back(digits[c & 15]);
        } else text.push_back(c);
    }
    text.push_back(quote);
    return text;
}
// str(OSError) without a filename, as Python reports pipe/fork/write failures.
std::u32string os_error_text(int code) {
    return points("[Errno " + std::to_string(code) + "] " + std::strerror(code));
}
#ifdef _WIN32
// str(OSError) from a Windows error: "[WinError N] message" with the trailing
// CR/LF and period removed, as PyErr_SetFromWindowsErr formats it.
std::u32string win_error_text(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = n && buffer ? std::wstring(buffer, n) : L"Windows Error 0x" + std::to_wstring(code);
    if (buffer) ::LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L'.')) message.pop_back();
    std::u32string text = points("[WinError " + std::to_string(code) + "] ");
    for (std::size_t i = 0; i < message.size(); ++i) {
        char32_t c = message[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < message.size() && message[i + 1] >= 0xdc00 && message[i + 1] < 0xe000)
            c = 0x10000 + ((c - 0xd800) << 10) + (message[++i] - 0xdc00);
        text.push_back(c);
    }
    return text;
}
#endif
std::string narrow(const std::u32string& text) {
    std::string out;
    for (const char32_t c : text) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(static_cast<char>(0xc0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
        else if (c < 0x10000) { out.push_back(static_cast<char>(0xe0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
        else { out.push_back(static_cast<char>(0xf0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 63))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
    }
    return out;
}
// BoundedLog: exclusive create, first LOG_LIMIT bytes retained, every byte
// counted, fsync at EOF. The reference's BufferedReader.read(4096) delivers
// whole 4096-byte chunks until EOF, so bytes are accounted in those units
// and a still-open pipe leaves the remainder uncounted, exactly as retained
// evidence records it. The reference stops reading at the first OSError;
// here the pipe keeps draining so the child cannot stall on it, and the
// first error alone is reported (message text is str(OSError) best effort).
constexpr std::size_t CHUNK = 4096;
struct Sink {
    fs::path path;
    int fd = -1;
    std::uint64_t total = 0;
    bool eof = false;
    std::optional<std::string> error;
    std::string pending;
    explicit Sink(fs::path p) : path(std::move(p)) {}
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;
    ~Sink() { close(); }
    // Python names the file only in the exclusive-open error; write and fsync
    // failures on the open file object carry no filename.
    void fail(int code, bool named) { if (!error) error = narrow(named ? os_error_text(code, path) : os_error_text(code)); close(); }
    void close() {
        if (fd < 0) return;
#ifdef _WIN32
        ::_close(fd);
#else
        ::close(fd);
#endif
        fd = -1;
    }
    void open() {
#ifdef _WIN32
        fd = ::_wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
#else
        fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
#endif
        if (fd < 0) fail(errno, true);
    }
    void consume(const char* data, std::size_t size) {
        pending.append(data, size);
        std::size_t used = 0;
        for (; pending.size() - used >= CHUNK; used += CHUNK) account(pending.data() + used, CHUNK);
        pending.erase(0, used);
    }
    void account(const char* data, std::size_t size) {
        const std::uint64_t keep = std::min<std::uint64_t>(size, total < LOG_LIMIT ? LOG_LIMIT - total : 0);
        total += size;
        for (std::uint64_t done = 0; fd >= 0 && done < keep;) {
#ifdef _WIN32
            const int put = ::_write(fd, data + done, static_cast<unsigned>(std::min<std::uint64_t>(keep - done, 1u << 30)));
#else
            const ssize_t put = ::write(fd, data + done, static_cast<std::size_t>(keep - done));
#endif
            if (put < 0) { if (errno == EINTR) continue; fail(errno, false); break; }
            done += static_cast<std::uint64_t>(put);
        }
    }
    void end() {
        eof = true;
        if (!pending.empty()) { account(pending.data(), pending.size()); pending.clear(); }
        if (fd < 0) return;
#ifdef _WIN32
        if (::_commit(fd) != 0) fail(errno, false);
#else
        if (::fsync(fd) != 0) fail(errno, false);
#endif
        close();
    }
    LogResult result() const {
        LogResult r;
        r.path = U"logs/" + store_paths::native_points(path.filename());
        r.total_bytes = total;
        r.retained_bytes = std::min<std::uint64_t>(total, LOG_LIMIT);
        r.truncated = total > LOG_LIMIT;
        if (error) r.error = error;
        else if (!eof) r.error = "log reader did not finish";
        return r;
    }
};
}

std::string_view reason_text(StopReason reason) {
    switch (reason) {
    case StopReason::timeout: return "timeout";
    case StopReason::cancelled: return "cancelled";
    default: return "exited";
    }
}

compat::Value LogResult::value() const {
    using namespace planning_internal;
    auto v = object_value();
    v.object.emplace_back(U"path", string_value(path));
    v.object.emplace_back(U"total_bytes", integer_value(std::to_string(total_bytes)));
    v.object.emplace_back(U"retained_bytes", integer_value(std::to_string(retained_bytes)));
    v.object.emplace_back(U"truncated", boolean_value(truncated));
    v.object.emplace_back(U"error", error ? string_value(points(*error)) : compat::Value());
    return v;
}

#ifndef _WIN32
namespace {
using process_internal::Fd;
// Popen's OSError before the child is running: no log, no on_started.
[[noreturn]] void spawn_failure(const char* what, const fs::path& path, int code, bool named) {
    throw SpawnError(narrow(named ? os_error_text(code, path) : os_error_text(code)), what, path,
                     std::error_code(code, std::generic_category()));
}
void above_stdio(Fd& fd, const fs::path& executable) {
    if (fd.fd >= 3) return;
    const int moved = ::fcntl(fd.fd, F_DUPFD_CLOEXEC, 3);
    if (moved < 0) spawn_failure("session stdin descriptor", executable, errno, false);
    fd.reset();
    fd.fd = moved;
}
struct Supervisor {
    const Spec& spec;
    process_internal::Child child{SUBJECT};
    Fd out_r, err_r;
    Sink out, err;
    Supervisor(const Spec& s, const fs::path& stdout_log, const fs::path& stderr_log)
        : spec(s), out(stdout_log), err(stderr_log) {}
    // Service both pipes for at most `wait`; a closed pipe pair just sleeps.
    void service(std::chrono::milliseconds wait) {
        pollfd fds[2];
        nfds_t count = 0;
        int out_at = -1, err_at = -1;
        if (out_r.fd >= 0) { out_at = static_cast<int>(count); fds[count++] = {out_r.fd, POLLIN, 0}; }
        if (err_r.fd >= 0) { err_at = static_cast<int>(count); fds[count++] = {err_r.fd, POLLIN, 0}; }
        const int ready = ::poll(count ? fds : nullptr, count, static_cast<int>(wait.count()));
        if (ready < 0) { if (errno == EINTR) return; os_failure("session poll", spec.executable, errno); }
        if (out_at >= 0 && fds[out_at].revents) drain(out_r, out);
        if (err_at >= 0 && fds[err_at].revents) drain(err_r, err);
    }
    void drain(Fd& fd, Sink& sink) {
        char buffer[65536];
        for (int reads = 0; reads < 4; ++reads) {
            const ssize_t got = ::read(fd.fd, buffer, sizeof buffer);
            if (got > 0) { sink.consume(buffer, static_cast<std::size_t>(got)); continue; }
            if (got == 0) { fd.reset(); sink.end(); return; }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            os_failure("session read", spec.executable, errno);
        }
    }
    bool wait_exit(std::chrono::milliseconds bound) {
        const auto until = Clock::now() + bound;
        while (!child.try_wait()) {
            if (Clock::now() >= until) return false;
            service(std::chrono::milliseconds(5));
        }
        return true;
    }
    // stop_owned: terminate, 0.5 s, kill, then the bounded reference reap.
    void stop_owned() {
        if (child.try_wait()) return;
        ::kill(child.pid, SIGTERM);
        if (wait_exit(GRACE)) return;
        ::kill(child.pid, SIGKILL);
        if (!wait_exit(REAP)) throw std::runtime_error("owned session child did not exit after SIGKILL");
    }
    // BoundedLog.finish: keep reading until EOF for at most the join bound.
    void finish() {
        const auto until = Clock::now() + FINISH;
        while ((out_r.fd >= 0 || err_r.fd >= 0) && Clock::now() < until) service(std::chrono::milliseconds(5));
        out_r.reset(); err_r.reset();
    }
};
}
Outcome run(const Spec& spec, const fs::path& stdout_log, const fs::path& stderr_log,
            const std::atomic<bool>* cancel,
            const std::function<void(long long pid, const std::string& started_at)>& on_started) {
    const fs::path& executable = spec.executable;
    Supervisor s(spec, stdout_log, stderr_log);
    Fd out_w, err_w, fail_r, fail_w, null_r;
    try {
        process_internal::make_pipe(s.out_r, out_w, executable, SUBJECT);
        process_internal::make_pipe(s.err_r, err_w, executable, SUBJECT);
        process_internal::make_pipe(fail_r, fail_w, executable, SUBJECT);
        // The parent's read ends are nonblocking before fork (a separate open
        // file description from the child's write ends), so nothing after a
        // successful exec can be mistaken for a spawn failure.
        process_internal::nonblocking(s.out_r.fd, executable, SUBJECT);
        process_internal::nonblocking(s.err_r.fd, executable, SUBJECT);
    } catch (const fs::filesystem_error& e) { spawn_failure("session pipe", executable, e.code().value(), false); }
    null_r.fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (null_r.fd < 0) spawn_failure("session stdin", fs::path("/dev/null"), errno, true);
    above_stdio(null_r, executable);
    // Everything the child needs is built before fork; only async-signal-safe
    // calls run between fork and exec.
    const std::string program = executable.string(), cwd = spec.cwd.string();
    std::vector<std::string> arguments;
    for (const auto& a : spec.argv) arguments.push_back(a.string());
    std::vector<char*> argv;
    for (const auto& a : arguments) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
#if !defined(__linux__) || !defined(SYS_close_range)
    spawn_failure("session requires close_range descriptor isolation", executable, ENOTSUP, false);
#endif
    process_internal::SigpipeGuard sigpipe;
    s.child.pid = ::fork();
    if (s.child.pid < 0) { s.child.pid = -1; spawn_failure("session fork", executable, errno, false); }
    if (s.child.pid == 0) {
        // Failure report: {stage, errno}; stage 0 is exec (reported against the
        // executable), stage 1 chdir (against cwd), and the dup2/setsid/
        // close_range stages carry no filename, exactly as Popen's noexec cases.
        auto fail = [&](int stage) {
            const int report[2] = {stage, errno};
            (void)!::write(fail_w.fd, report, sizeof report);
            ::_exit(127);
        };
        if (::dup2(null_r.fd, 0) < 0 || ::dup2(out_w.fd, 1) < 0 || ::dup2(err_w.fd, 2) < 0) fail(2);
        if (::chdir(cwd.c_str()) != 0) fail(1);
        if (::setsid() < 0) fail(3);
#if defined(__linux__) && defined(SYS_close_range)
        const long low = fail_w.fd == 3 ? 0 : ::syscall(SYS_close_range, 3u, static_cast<unsigned>(fail_w.fd - 1), 0u);
        const int low_error = errno;
        const long high = ::syscall(SYS_close_range, static_cast<unsigned>(fail_w.fd + 1), ~0u, 0u);
        if (low != 0 || high != 0) { errno = low != 0 ? low_error : errno; fail(4); }
#endif
        sigset_t none;
        ::sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        ::signal(SIGPIPE, SIG_DFL);
        ::execv(program.c_str(), argv.data());
        fail(0);
    }
    null_r.reset(); out_w.reset(); err_w.reset(); fail_w.reset();
    // Popen reads the exec-error pipe to EOF before returning: spawn failure
    // is synchronous, reaps the child and creates no log file.
    std::string report;
    for (;;) {
        char bytes[2 * sizeof(int)];
        const ssize_t got = ::read(fail_r.fd, bytes, sizeof bytes);
        if (got > 0) { report.append(bytes, static_cast<std::size_t>(got)); continue; }
        if (got == 0) break;
        if (errno != EINTR) spawn_failure("session exec handshake", executable, errno, false);
    }
    fail_r.reset();
    if (!report.empty()) {
        int status;
        pid_t got;
        do { got = ::waitpid(s.child.pid, &status, 0); } while (got < 0 && errno == EINTR);
        s.child.reaped = true;
        if (report.size() != 2 * sizeof(int)) spawn_failure("session exec handshake", executable, EIO, false);
        int stage, code;
        std::memcpy(&stage, report.data(), sizeof stage);
        std::memcpy(&code, report.data() + sizeof stage, sizeof code);
        if (stage == 0) spawn_failure("session exec", executable, code, true);
        if (stage == 1) spawn_failure("session cwd", spec.cwd, code, true);
        spawn_failure("session child setup", executable, code, false);
    }
    Outcome outcome;
    outcome.pid = s.child.pid;
    outcome.started_at = store_write::now();
    s.out.open(); s.err.open();
    try {
        on_started(outcome.pid, outcome.started_at);
        const auto launched = Clock::now();
        while (!s.child.try_wait()) {
            if (cancel && cancel->load()) { outcome.reason = StopReason::cancelled; s.stop_owned(); break; }
            if (std::chrono::duration<double>(Clock::now() - launched) >= spec.timeout) {
                outcome.reason = StopReason::timeout; s.stop_owned(); break;
            }
            s.service(TICK);
        }
    } catch (...) {
        // Even a failed running-journal write must not leave an owned child.
        try { s.stop_owned(); } catch (...) {}
        try { s.finish(); } catch (...) {}
        throw;
    }
    outcome.returned_at = store_write::now();
    s.finish();
    outcome.exit_code = s.child.returncode();
    outcome.out = s.out.result();
    outcome.err = s.err.result();
    return outcome;
}
#else
namespace {
using process_internal::Handle;
// Popen's OSError before the child is running (CreateProcess and its setup
// report a WinError without a filename).
[[noreturn]] void spawn_failure(const char* what, const fs::path& path, DWORD code) {
    throw SpawnError(narrow(win_error_text(code)), what, path, std::error_code(static_cast<int>(code), std::system_category()));
}
struct Supervisor {
    const Spec& spec;
    Handle out_r, err_r, process, job;
    Sink out, err;
    bool exited = false;
    Supervisor(const Spec& s, const fs::path& stdout_log, const fs::path& stderr_log)
        : spec(s), out(stdout_log), err(stderr_log) {}
    bool drain(Handle& handle, Sink& sink) {
        if (!handle.h) return false;
        char bytes[65536];
        DWORD got = 0;
        if (!::ReadFile(handle.h, bytes, sizeof bytes, &got, nullptr)) {
            const DWORD code = ::GetLastError();
            if (code == ERROR_BROKEN_PIPE) { handle.reset(); sink.end(); return true; }
            if (code == ERROR_NO_DATA) return false;
            os_failure("session read", spec.executable, static_cast<int>(code));
        }
        if (got) sink.consume(bytes, got);
        return got != 0;
    }
    bool try_wait() {
        if (exited) return true;
        const DWORD waited = ::WaitForSingleObject(process.h, 0);
        if (waited == WAIT_OBJECT_0) exited = true;
        else if (waited != WAIT_TIMEOUT) os_failure("session wait", spec.executable, static_cast<int>(::GetLastError()));
        return exited;
    }
    void service(std::chrono::milliseconds wait) {
        const auto until = Clock::now() + wait;
        do {
            bool progress = drain(out_r, out);
            progress = drain(err_r, err) || progress;
            if (progress) return;
            ::Sleep(1);
        } while (Clock::now() < until);
    }
    bool wait_exit(std::chrono::milliseconds bound) {
        const auto until = Clock::now() + bound;
        while (!try_wait()) {
            if (Clock::now() >= until) return false;
            service(std::chrono::milliseconds(5));
        }
        return true;
    }
    // Popen.terminate and kill are both TerminateProcess(handle, 1).
    void stop_owned() {
        if (try_wait()) return;
        if (!::TerminateProcess(process.h, 1) && ::GetLastError() != ERROR_ACCESS_DENIED)
            os_failure("session terminate", spec.executable, static_cast<int>(::GetLastError()));
        if (wait_exit(GRACE)) return;
        ::TerminateProcess(process.h, 1);
        if (!wait_exit(REAP)) throw std::runtime_error("owned session child did not exit after termination");
    }
    void finish() {
        const auto until = Clock::now() + FINISH;
        while ((out_r.h || err_r.h) && Clock::now() < until) service(std::chrono::milliseconds(5));
        out_r.reset(); err_r.reset();
    }
};
}
Outcome run(const Spec& spec, const fs::path& stdout_log, const fs::path& stderr_log,
            const std::atomic<bool>* cancel,
            const std::function<void(long long pid, const std::string& started_at)>& on_started) {
    const fs::path& executable = spec.executable;
    Supervisor s(spec, stdout_log, stderr_log);
    Handle out_w, err_w, null_r;
    try {
        process_internal::make_pipe(s.out_r, out_w, false, executable, SUBJECT);
        process_internal::make_pipe(s.err_r, err_w, false, executable, SUBJECT);
    } catch (const fs::filesystem_error& e) { spawn_failure("session pipe", executable, static_cast<DWORD>(e.code().value())); }
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    null_r.h = ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr);
    if (null_r.h == INVALID_HANDLE_VALUE) { null_r.h = nullptr; spawn_failure("session stdin", executable, ::GetLastError()); }
    // list2cmdline over spec argv (argv[0] included); the executable is the
    // application name, exactly as Popen(argv, executable=path) passes them.
    std::wstring line;
    for (const auto& a : spec.argv) process_internal::append_argument(line, a.wstring());
    if (line.empty()) line = L"\"\"";
    HANDLE inherited[3] = {null_r.h, out_w.h, err_w.h};
    SIZE_T size = 0;
    if (::InitializeProcThreadAttributeList(nullptr, 1, 0, &size) || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        spawn_failure("session spawn attributes", executable, ::GetLastError());
    std::vector<char> storage(size);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!::InitializeProcThreadAttributeList(list, 1, 0, &size)) spawn_failure("session spawn", executable, ::GetLastError());
    struct ListGuard { LPPROC_THREAD_ATTRIBUTE_LIST l; ~ListGuard() { ::DeleteProcThreadAttributeList(l); } } list_guard{list};
    if (!::UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited, nullptr, nullptr))
        spawn_failure("session spawn", executable, ::GetLastError());
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = null_r.h;
    startup.StartupInfo.hStdOutput = out_w.h;
    startup.StartupInfo.hStdError = err_w.h;
    startup.lpAttributeList = list;
    // The job kills the child if this process dies or any path leaves early.
    s.job.h = ::CreateJobObjectW(nullptr, nullptr);
    if (!s.job.h) spawn_failure("session job", executable, ::GetLastError());
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(s.job.h, JobObjectExtendedLimitInformation, &limits, sizeof limits))
        spawn_failure("session job", executable, ::GetLastError());
    PROCESS_INFORMATION info{};
    const std::wstring application = fs::absolute(executable).wstring(), directory = spec.cwd.wstring();
    if (!::CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, TRUE,
                          CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                          directory.empty() ? nullptr : directory.c_str(), &startup.StartupInfo, &info))
        spawn_failure("session spawn", executable, ::GetLastError());
    Handle thread;
    s.process.h = info.hProcess;
    thread.h = info.hThread;
    struct Kill {
        HANDLE process, job;
        bool assigned = false, armed = true;
        ~Kill() {
            if (armed) {
                if (assigned) ::TerminateJobObject(job, 1);
                else ::TerminateProcess(process, 1);
            }
        }
    } kill{s.process.h, s.job.h};
    if (!::AssignProcessToJobObject(s.job.h, s.process.h)) spawn_failure("session job", executable, ::GetLastError());
    kill.assigned = true;
    if (::ResumeThread(thread.h) == static_cast<DWORD>(-1)) spawn_failure("session resume", executable, ::GetLastError());
    null_r.reset(); out_w.reset(); err_w.reset();
    Outcome outcome;
    outcome.pid = static_cast<long long>(info.dwProcessId);
    outcome.started_at = store_write::now();
    s.out.open(); s.err.open();
    try {
        on_started(outcome.pid, outcome.started_at);
        const auto launched = Clock::now();
        while (!s.try_wait()) {
            if (cancel && cancel->load()) { outcome.reason = StopReason::cancelled; s.stop_owned(); break; }
            if (std::chrono::duration<double>(Clock::now() - launched) >= spec.timeout) {
                outcome.reason = StopReason::timeout; s.stop_owned(); break;
            }
            s.service(TICK);
        }
    } catch (...) {
        try { s.stop_owned(); } catch (...) {}
        try { s.finish(); } catch (...) {}
        throw;
    }
    outcome.returned_at = store_write::now();
    s.finish();
    DWORD code = 0;
    if (!::GetExitCodeProcess(s.process.h, &code)) os_failure("session exit code", executable, static_cast<int>(::GetLastError()));
    // Descendants holding the pipes past the log bound die with the job.
    if (!::TerminateJobObject(s.job.h, 1)) os_failure("session job cleanup", executable, static_cast<int>(::GetLastError()));
    kill.armed = false;
    outcome.exit_code = static_cast<long long>(code); // the unsigned DWORD, as Popen.returncode records it
    outcome.out = s.out.result();
    outcome.err = s.err.result();
    return outcome;
}
#endif
}
