// Test-only driver for the private probe runner; authored helpers only.
#include "probe_process.hpp"
#include "capture.hpp"
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include "probe_process_test.hpp"
#include <cerrno>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>
#endif
using namespace c2::frontend;
namespace fs = std::filesystem;
namespace {
std::string hex(const std::string& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const unsigned char c : bytes) { out.push_back(digits[c >> 4]); out.push_back(digits[c & 15]); }
    return out;
}
std::optional<fs::path> optional_path(const std::string& text) {
    if (text == "-") return std::nullopt;
    return fs::u8path(text);
}

#ifndef _WIN32
struct DeferredReap : probe_process::testing::ReapObserver {
    std::mutex mutex;
    std::condition_variable changed;
    pid_t child = -1;
    int signals = 0, synchronous_waits = 0, transfers = 0, acquired = 0, completed = 0;
    bool release = false, invalid = false;
    DeferredReap() { defer_synchronous_reap = true; }
    void observe(probe_process::testing::ReapEvent event, pid_t pid, int status) noexcept override {
        using Event = probe_process::testing::ReapEvent;
        std::unique_lock<std::mutex> lock(mutex);
        invalid |= pid <= 0 || pid != child;
        switch (event) {
        case Event::signal:
            invalid |= transfers != 0 || signals++ != 0;
            break;
        case Event::synchronous_wait:
            invalid |= transfers != 0 || signals != 1;
            ++synchronous_waits;
            break;
        case Event::transfer:
            invalid |= transfers++ != 0 || synchronous_waits == 0;
            break;
        case Event::waiter_acquired:
            invalid |= transfers != 1 || acquired++ != 0;
            changed.notify_all();
            changed.wait(lock, [&] { return release; });
            break;
        case Event::waiter_reaped:
            invalid |= !release || acquired != 1 || completed++ != 0 ||
                !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL;
            changed.notify_all();
            break;
        }
    }
};
// Always release the waiter, including assertion/exception paths. The waiter
// keeps its own shared ownership; all waits here have an outer process timeout.
struct ReapScope {
    std::shared_ptr<DeferredReap> state = std::make_shared<DeferredReap>();
    ReapScope() { probe_process::testing::reap_observer = state; }
    ~ReapScope() {
        probe_process::testing::reap_observer.reset();
        std::unique_lock<std::mutex> lock(state->mutex);
        state->release = true;
        state->changed.notify_all();
        if (state->transfers)
            state->changed.wait_for(lock, std::chrono::seconds(5), [&] { return state->completed != 0; });
    }
};
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void deferred_reap(const fs::path& helper, const fs::path& ready_fifo) {
    ReapScope scope;
    const auto state = scope.state;
    struct InjectedFailure {};
    bool failed = false;
    const auto start = std::chrono::steady_clock::now();
    try {
        probe_process::run(helper, {"hold", ready_fifo.string()}, "", std::chrono::seconds(5), 65536, [&] {
            // The compiled helper writes its PID to this FIFO before holding.
            // This blocks until real exec/initialization, without polling sleeps.
            std::ifstream ready(ready_fifo);
            pid_t child = -1;
            ready >> child;
            require(ready.good() || ready.eof(), "helper readiness read failed");
            require(child > 0, "helper did not report its PID");
            { std::lock_guard<std::mutex> lock(state->mutex); state->child = child; }
            throw InjectedFailure{};
        });
    } catch (const InjectedFailure&) { failed = true; }
    // run() and its Child stack are gone while the waiter is still gated.
    probe_process::testing::reap_observer.reset();
    require(failed, "injected failure did not return");
    require(std::chrono::steady_clock::now() - start < std::chrono::seconds(5), "handoff return exceeded bound");
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        require(state->changed.wait_for(lock, std::chrono::seconds(5), [&] { return state->acquired != 0; }),
                "waiter did not acquire child");
        require(!state->invalid && state->signals == 1 && state->transfers == 1 && state->completed == 0,
                "ownership sequence invalid before release");
        // Observe the actual child without consuming its wait status. The
        // exclusive waiter must still own it; a synchronous reap would fail.
        siginfo_t info{};
        require(::waitid(P_PID, static_cast<id_t>(state->child), &info, WEXITED | WNOWAIT) == 0 &&
                info.si_pid == state->child && info.si_code == CLD_KILLED && info.si_status == SIGKILL,
                "owned child not available to waiter");
        state->release = true;
        state->changed.notify_all();
        require(state->changed.wait_for(lock, std::chrono::seconds(5), [&] { return state->completed != 0; }),
                "waiter did not complete real waitpid");
        require(!state->invalid && state->acquired == 1 && state->completed == 1,
                "ownership sequence invalid after release");
    }
    int status = 0;
    errno = 0;
    require(::waitpid(state->child, &status, WNOHANG) == -1 && errno == ECHILD, "child was not reaped");
    const auto result = probe_process::run(helper, {"args", "after"}, "", std::chrono::seconds(5));
    require(result.returncode == 0 && result.out == std::string("after\0", 6) && result.err.empty(),
            "subsequent helper in same process failed");
    std::cout << "ok deferred-reaped-once\n";
}
#endif
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 2) return 2;
    const std::string command = argv[1];
    try {
        if (command == "io-failure" && argc == 3) {
            try {
                probe_process::run(fs::u8path(argv[2]), {"hold"}, "", std::chrono::seconds(5), 65536,
                    [] { throw fs::filesystem_error("injected pipe I/O failure", std::make_error_code(std::errc::io_error)); });
                return 3;
            } catch (const fs::filesystem_error&) {}
            // Cleanup must leave this process usable for another real spawn.
            const auto result = probe_process::run(fs::u8path(argv[2]), {"args", "after"}, "", std::chrono::seconds(5));
            std::cout << "ok " << hex(result.out) << '\n';
        } else if ((command == "inheritance" || command == "inheritance-high") && argc == 3) {
#ifdef _WIN32
            SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
            HANDLE handle = ::CreateEventW(&attributes, TRUE, FALSE, nullptr);
            if (!handle) return 3;
            const auto number = std::to_string(reinterpret_cast<std::uintptr_t>(handle));
#else
            int handle = ::open("/dev/null", O_RDONLY);
            if (handle < 0) return 3;
            if (command == "inheritance-high") {
                const int high = ::fcntl(handle, F_DUPFD, 256);
                if (high < 0) return 3;
                ::close(handle); handle = high;
                const rlimit lower{64, 64};
                if (::setrlimit(RLIMIT_NOFILE, &lower) != 0) return 3;
            }
            const auto number = std::to_string(handle);
#endif
            const auto result = probe_process::run(fs::u8path(argv[2]), {"inherited", number}, "", std::chrono::seconds(5));
#ifdef _WIN32
            ::CloseHandle(handle);
#else
            ::close(handle);
#endif
            std::cout << "ok " << result.returncode << '\n';
#ifndef _WIN32
        } else if (command == "deferred-reap" && argc == 4) {
            try { deferred_reap(fs::u8path(argv[2]), fs::u8path(argv[3])); }
            catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 3; }
        } else if (command == "closed-stdio" && argc == 3) {
            const int report = ::fcntl(1, F_DUPFD_CLOEXEC, 10);
            if (report < 0) return 3;
            ::close(0); ::close(1); ::close(2);
            const auto result = probe_process::run(fs::u8path(argv[2]), {"args", "stdio"}, "", std::chrono::seconds(5));
            const std::string message = "ok " + hex(result.out) + "\n";
            (void)!::write(report, message.data(), message.size());
            ::close(report);
#endif
        } else if (command == "inspect" && argc == 6) {
            const std::string content((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
            const auto value = probe_process::codec_inspect(content, argv[2], optional_path(argv[4]), argv[3],
                                                            std::chrono::milliseconds(std::stol(argv[5])));
            std::cout << "ok " << compat::compact(value) << '\n';
        } else if (command == "run" && argc >= 5) {
            // run <timeout_ms> <limit> <executable> [arguments...]; stdin is the input.
            const std::string content((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
            std::vector<std::string> arguments(argv + 5, argv + argc);
            const auto result = probe_process::run(fs::u8path(argv[4]), arguments, content,
                std::chrono::milliseconds(std::stol(argv[2])), static_cast<std::size_t>(std::stoull(argv[3])));
            std::cout << "ok " << result.returncode << ' ' << hex(result.out) << ' ' << hex(result.err) << '\n';
        } else if (command == "inspect-set" && argc == 6) {
            // inspect-set <root> <key> <dialect> <probe|->
            const auto inventory = inventory_profiles(fs::u8path(argv[2]));
            const std::string key = argv[3];
            for (const auto& state : inventory.states())
                if (state.key() == std::u32string(key.begin(), key.end())) {
                    const auto value = probe_process::inspect_set(state, optional_path(argv[5]), argv[4]);
                    std::cout << "ok " << compat::compact(value) << '\n';
                    return 0;
                }
            std::cout << "missing\n";
        } else if (command == "inspect-bytes" && argc == 5) {
            const auto captured = capture(fs::u8path(argv[2]));
            const auto value = probe_process::inspect_bytes(captured.blobs, std::stoi(argv[3]), fs::u8path(argv[4]));
            std::cout << "ok " << compat::compact(value) << '\n';
        } else if (command == "evidence" && argc == 3) {
            const auto value = probe_process::codec_evidence(optional_path(argv[2]));
            std::cout << "ok " << compat::compact(value) << '\n';
        } else return 2;
    } catch (const probe_process::Timeout& e) { std::cout << "timeout " << e.what() << '\n';
    } catch (const probe_process::ProbeError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const probe_process::OutputLimit& e) { std::cout << "limit " << e.what() << '\n';
    } catch (const fs::filesystem_error& e) { std::cout << "oserror " << e.code().value() << '\n';
    } catch (const std::invalid_argument& e) { std::cout << "invalid " << e.what() << '\n';
    } catch (const compat::Error& e) { std::cout << "json " << e.what() << '\n'; }
    return 0;
}
