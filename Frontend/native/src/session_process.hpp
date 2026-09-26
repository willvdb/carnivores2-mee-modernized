#pragma once
// PRIVATE owned engine/fixture child supervisor (workstream C, 4A/4B). Not the
// codec probe runner: continuous draining into bounded retained logs, deadline,
// cancellation, owned-child-only termination and cleanup on every path. Never
// reads an executable from a journal; the caller passes the freshly revalidated spec.
#include "json_compat.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace c2::frontend::session_process {
inline constexpr std::size_t LOG_LIMIT = 64u * 1024u; // session_runner.LOG_LIMIT
struct Spec {
    std::filesystem::path executable;           // spec['executable']['path']
    std::vector<std::filesystem::path> argv;    // spec['argv'] (argv[0] included)
    std::filesystem::path cwd;                  // spec['cwd']
    std::chrono::duration<double> timeout;      // spec['timeout_seconds']
};
enum class StopReason { exited, timeout, cancelled };
std::string_view reason_text(StopReason); // "exited" | "timeout" | "cancelled"
// BoundedLog.finish() result; path is "logs/<name>".
struct LogResult {
    std::u32string path;
    std::uint64_t total_bytes = 0, retained_bytes = 0;
    bool truncated = false;
    std::optional<std::string> error; // null when capture succeeded
    compat::Value value() const;      // {'path','total_bytes','retained_bytes','truncated','error'}
};
struct Outcome {
    long long pid = 0;
    std::string started_at, returned_at; // store_write::now() at the reference points
    long long exit_code = 0;             // Popen.returncode: -signal on POSIX, the unsigned DWORD on Windows
    StopReason reason = StopReason::exited;
    LogResult out, err;                  // stdout, stderr
};
// Popen's OSError: what() is the reference str(OSError) text ("[Errno N]
// strerror: 'path'" with the executable or cwd where Python names one, without
// a filename otherwise; "[WinError N] message" on Windows), which the runner
// records verbatim as the spawn-failed diagnostic.
class SpawnError : public std::filesystem::filesystem_error {
public:
    SpawnError(std::string text, const std::string& what, const std::filesystem::path& path, std::error_code code)
        : std::filesystem::filesystem_error(what, path, code), text_(std::move(text)) {}
    const char* what() const noexcept override { return text_.c_str(); }
private:
    std::string text_;
};
// Spawns shell-free (stdin null, stdout/stderr pipes, new session on POSIX,
// descriptor isolation). Spawn failure throws SpawnError (a
// std::filesystem::filesystem_error) before any log file exists and before on_started. Logs are created
// exclusively ('xb'). on_started(pid, started_at) is called once after spawn
// (the runner persists the 'running' transition there); if it throws, the
// child is stopped, logs finished, and the exception propagates. cancel may be null.
Outcome run(const Spec&, const std::filesystem::path& stdout_log, const std::filesystem::path& stderr_log,
            const std::atomic<bool>* cancel,
            const std::function<void(long long pid, const std::string& started_at)>& on_started);
}
