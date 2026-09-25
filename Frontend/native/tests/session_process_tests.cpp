// Test-only driver for the private owned-child supervisor; authored child only.
// run <executable> <cwd> <timeout_seconds> <stdout_log> <stderr_log> <cancel_ms|-> <started: ok|throw> -- argv...
#include "session_process.hpp"
#include "json_compat.hpp"
#include "planning_internal.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#include <sys/wait.h>
#endif
using namespace c2::frontend;
namespace fs = std::filesystem;
namespace {
struct InjectedFailure {};
// Whether the owned child is no longer alive or owned by this process.
bool child_gone(long long pid) {
#ifdef _WIN32
    HANDLE handle = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!handle) return true;
    const bool exited = ::WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
    ::CloseHandle(handle);
    return exited;
#else
    errno = 0;
    if (::waitpid(static_cast<pid_t>(pid), nullptr, WNOHANG) != -1 || errno != ECHILD) return false;
    return ::kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH;
#endif
}
compat::Value text(const std::string& ascii) { return planning_internal::ascii_value(ascii); }
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 10 || std::string(argv[1]) != "run" || std::string(argv[9]) != "--") return 2;
    session_process::Spec spec;
    spec.executable = fs::u8path(argv[2]);
    spec.cwd = fs::u8path(argv[3]);
    spec.timeout = std::chrono::duration<double>(std::stod(argv[4]));
    for (int i = 10; i < argc; ++i) spec.argv.push_back(fs::u8path(argv[i]));
    const fs::path stdout_log = fs::u8path(argv[5]), stderr_log = fs::u8path(argv[6]);
    const std::string cancel_text = argv[7], started_mode = argv[8];
    std::atomic<bool> cancel{false};
    std::thread canceller;
    if (cancel_text != "-") {
        const auto delay = std::chrono::milliseconds(std::stol(cancel_text));
        canceller = std::thread([&cancel, delay] { std::this_thread::sleep_for(delay); cancel.store(true); });
    }
    int started_calls = 0;
    long long started_pid = -1;
    std::string started_at_seen;
    const auto begin = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(); };
    auto on_started = [&](long long pid, const std::string& started_at) {
        ++started_calls; started_pid = pid; started_at_seen = started_at;
        if (started_mode == "throw") throw InjectedFailure{};
    };
    using namespace planning_internal;
    auto report = object_value();
    try {
        const auto outcome = session_process::run(spec, stdout_log, stderr_log, cancel_text == "-" ? nullptr : &cancel, on_started);
        report.object.emplace_back(U"kind", text("ok"));
        report.object.emplace_back(U"pid", integer_value(std::to_string(outcome.pid)));
        report.object.emplace_back(U"started_at", text(outcome.started_at));
        report.object.emplace_back(U"returned_at", text(outcome.returned_at));
        report.object.emplace_back(U"exit_code", integer_value(std::to_string(outcome.exit_code)));
        report.object.emplace_back(U"reason", text(std::string(session_process::reason_text(outcome.reason))));
        auto logs = object_value();
        logs.object.emplace_back(U"stdout", outcome.out.value());
        logs.object.emplace_back(U"stderr", outcome.err.value());
        report.object.emplace_back(U"logs", std::move(logs));
        report.object.emplace_back(U"child_gone", boolean_value(child_gone(outcome.pid)));
        report.object.emplace_back(U"started_pid_matches", boolean_value(started_pid == outcome.pid));
        report.object.emplace_back(U"started_at_matches", boolean_value(started_at_seen == outcome.started_at));
    } catch (const InjectedFailure&) {
        report.object.emplace_back(U"kind", text("started-threw"));
        report.object.emplace_back(U"pid", integer_value(std::to_string(started_pid)));
        report.object.emplace_back(U"child_gone", boolean_value(child_gone(started_pid)));
    } catch (const fs::filesystem_error& e) {
        report.object.emplace_back(U"kind", text("oserror"));
        report.object.emplace_back(U"code", integer_value(std::to_string(e.code().value())));
        report.object.emplace_back(U"what", text(e.what()));
        report.object.emplace_back(U"path", text(e.path1().string()));
    } catch (const std::exception& e) {
        report.object.emplace_back(U"kind", text("exception"));
        report.object.emplace_back(U"what", text(e.what()));
    }
    if (canceller.joinable()) canceller.join();
    report.object.emplace_back(U"started_calls", integer_value(std::to_string(started_calls)));
    report.object.emplace_back(U"elapsed", text(std::to_string(elapsed())));
    report.object.emplace_back(U"stdout_log_exists", boolean_value(fs::exists(stdout_log)));
    report.object.emplace_back(U"stderr_log_exists", boolean_value(fs::exists(stderr_log)));
    std::cout << compat::compact(report) << '\n';
    return 0;
}
