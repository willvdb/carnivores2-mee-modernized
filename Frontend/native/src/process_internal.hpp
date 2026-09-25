#pragma once
// PRIVATE process primitives shared by the codec probe runner and the owned
// session child supervisor: descriptor/handle owners, CLOEXEC pipes, the
// prestarted POSIX cleanup waiter, SIGPIPE guarding and Windows argument
// quoting. `subject` prefixes every OS failure ("probe pipe", "session pipe").
// Not a process abstraction: each runner keeps its own spawn and drain loop.
#include "probe_process_test.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "store_write.hpp"
#else
#include <atomic>
#include <fcntl.h>
#include <memory>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif
namespace c2::frontend::process_internal {
using Clock = std::chrono::steady_clock;
[[noreturn]] inline void os_failure(const std::string& what, const std::filesystem::path& path, int code) {
#ifdef _WIN32
    throw std::filesystem::filesystem_error(what, path, std::error_code(code, std::system_category()));
#else
    throw std::filesystem::filesystem_error(what, path, std::error_code(code, std::generic_category()));
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
inline void make_pipe(Fd& read_end, Fd& write_end, const std::filesystem::path& executable, const std::string& subject) {
    int fds[2];
#if defined(__linux__)
    if (::pipe2(fds, O_CLOEXEC) != 0) os_failure(subject + " pipe", executable, errno);
#else
    if (::pipe(fds) != 0) os_failure(subject + " pipe", executable, errno);

#endif
    read_end.fd = fds[0];
    write_end.fd = fds[1];
    // Reserve 0..2 for dup2 even when the caller started with closed stdio.
    for (Fd* end : {&read_end, &write_end}) {
        if (end->fd < 3) {
            const int moved = ::fcntl(end->fd, F_DUPFD_CLOEXEC, 3);
            if (moved < 0) os_failure(subject + " pipe descriptor", executable, errno);
            end->reset();
            end->fd = moved;
        } else if (::fcntl(end->fd, F_SETFD, FD_CLOEXEC) < 0) {
            os_failure(subject + " pipe flags", executable, errno);
        }
    }
}
inline void nonblocking(int fd, const std::filesystem::path& executable, const std::string& subject) {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        os_failure(subject + " pipe flags", executable, errno);
}
// A cleanup waiter is allocated and started BEFORE fork, so thread startup
// failure cannot strand a child. Normal operation alone owns waitpid/kill.
// On failure it sends SIGKILL, then transfers the owned PID to this waiter;
// a kernel-delayed exit cannot make the caller's deadline an unbounded reap.
// No reference to the runner's stack escapes and no thread is ever joined.
struct Child {
    std::shared_ptr<probe_process::testing::ReapObserver> observer = probe_process::testing::reap_observer;
    void observe(probe_process::testing::ReapEvent event) const noexcept {
        if (observer) observer->observe(event, pid);
    }
    std::shared_ptr<std::atomic<pid_t>> cleanup = std::make_shared<std::atomic<pid_t>>(-2);
    pid_t pid = -1;
    bool reaped = false;
    int status = 0;
    std::string subject;
    explicit Child(std::string subject_) : subject(std::move(subject_)) {
        std::thread([state = cleanup, observer = observer] {
            pid_t owned;
            while ((owned = state->load()) == -2) ::usleep(1000);
            if (owned > 0) {
                if (observer) observer->observe(probe_process::testing::ReapEvent::waiter_acquired, owned);
                int status = 0;
                pid_t got;
                do { got = ::waitpid(owned, &status, 0); } while (got < 0 && errno == EINTR);
                if (observer) observer->observe(probe_process::testing::ReapEvent::waiter_reaped, got, status);
            }
        }).detach();
    }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    ~Child() {
        if (pid > 0 && !reaped) {
            observe(probe_process::testing::ReapEvent::signal);
            ::kill(pid, SIGKILL);
            // Usually reap immediately; otherwise the already-running waiter
            // retains exclusive ownership until the child actually exits.
            const auto reap_deadline = Clock::now() + std::chrono::milliseconds(250);
            do {
                observe(probe_process::testing::ReapEvent::synchronous_wait);
                const pid_t got = observer && observer->defer_synchronous_reap
                    ? 0 : ::waitpid(pid, &status, WNOHANG);
                if (got == pid || (got < 0 && errno == ECHILD)) { reaped = true; break; }
                ::usleep(1000);
            } while (Clock::now() < reap_deadline);
        }
        if (pid > 0 && !reaped) observe(probe_process::testing::ReapEvent::transfer);
        cleanup->store(pid > 0 && !reaped ? pid : -1);
    }
    bool try_wait() {
        const pid_t got = ::waitpid(pid, &status, WNOHANG);
        if (got < 0 && errno != EINTR) {
            const int code = errno;
            if (code == ECHILD) reaped = true; // never signal a no-longer-owned PID
            os_failure(subject + " wait", {}, code);
        }
        if (got == pid) reaped = true;
        return reaped;
    }
    // Popen.returncode: exit status, or -signal on termination.
    int returncode() const {
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return -WTERMSIG(status);
        return status;
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
#else
struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    void reset() { if (h && h != INVALID_HANDLE_VALUE) ::CloseHandle(h); h = nullptr; }
};
// subprocess.list2cmdline quoting (MS C runtime argument rules).
inline void append_argument(std::wstring& line, const std::wstring& argument) {
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
inline std::wstring widen(const std::string& utf8, const std::string& subject) {
    if (utf8.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) throw std::invalid_argument(subject + " argument is not UTF-8");
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), wide.data(), n);
    return wide;
}
inline void make_pipe(Handle& read_end, Handle& write_end, bool child_reads, const std::filesystem::path& executable,
                      const std::string& subject) {
    // Parent ends are synchronous, nonblocking byte-mode named pipes. There
    // are no worker threads, pending OVERLAPPED buffers, cancellation races,
    // or joins. Children receive ordinary blocking handles. Every operation
    // in the owner loop returns immediately (PIPE_NOWAIT).
    const std::string id = store_write::new_id();
    const std::wstring name = L"\\\\.\\pipe\\c2-" + std::wstring(subject.begin(), subject.end()) + L"-" +
                              std::wstring(id.begin(), id.end());
    Handle& parent = child_reads ? write_end : read_end;
    Handle& child = child_reads ? read_end : write_end;
    parent.h = ::CreateNamedPipeW(name.c_str(),
        (child_reads ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND) | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 65536, 65536, 0, nullptr);
    if (parent.h == INVALID_HANDLE_VALUE) os_failure(subject + " pipe", executable, static_cast<int>(::GetLastError()));
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    child.h = ::CreateFileW(name.c_str(), child_reads ? GENERIC_READ : GENERIC_WRITE,
        0, &attributes, OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
    if (child.h == INVALID_HANDLE_VALUE) os_failure(subject + " pipe client", executable, static_cast<int>(::GetLastError()));
    if (!::ConnectNamedPipe(parent.h, nullptr)) {
        const DWORD code = ::GetLastError();
        if (code != ERROR_PIPE_CONNECTED) os_failure(subject + " pipe connect", executable, static_cast<int>(code));
    }
}
#endif
}
