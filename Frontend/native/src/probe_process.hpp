#pragma once
// PRIVATE, narrowly pulled-forward part of 4A: run the caller-configured
// profile codec helper exactly as lodge.profiles.codec_inspect does
// (subprocess.run([probe, kind], input=bytes, capture_output=True,
// timeout=15, check=False)). Shell-free: explicit executable and argument
// vector, binary pipes, owned-child cleanup on every path. Not a generic
// process abstraction, engine runner or scheduler. The executable is always
// supplied by the caller (argument or C2_PROFILE_PROBE); nothing here reads a
// path out of a journal.
#include "json_compat.hpp"
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace c2::frontend::probe_process {
// Carries only reference FrontendError messages.
class ProbeError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
// The child was killed and reaped before this is thrown.
class Timeout : public ProbeError { public: using ProbeError::ProbeError; };
// Native-only bound (the reference buffers without limit): the child is
// killed and reaped, then this is thrown. Never truncates into a result.
class OutputLimit : public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct Result {
    // subprocess returncode: exit status, or -signal on POSIX termination.
    int returncode = 0;
    std::string out, err;
};
constexpr std::size_t default_output_limit = 16u * 1024u * 1024u;
// Spawn failure throws std::filesystem::filesystem_error (reference OSError).
// The executable path is used as given, without PATH search: a bare name
// resolves against the working directory, not PATH (documented divergence;
// session callers always pass the resolved executable_evidence path).
Result run(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
           std::string_view input, std::chrono::milliseconds timeout,
           std::size_t output_limit = default_output_limit);
// sessions.executable_evidence: expanduser + strict resolve, regular file,
// {'path', 'sha256'} in that order.
compat::Value executable_evidence(const std::filesystem::path&);
// sessions.codec_evidence: explicit probe, else C2_PROFILE_PROBE, else the
// reference refusal.
compat::Value codec_evidence(const std::optional<std::filesystem::path>& probe);
// profiles.codec_inspect, including the dialect/size short circuits before
// any helper lookup, the 15 s timeout and name_display_latin1 derivation.
// A helper result that is not a JSON object is returned unchanged (the
// reference's membership test on other kinds is not reproduced).
compat::Value codec_inspect(std::string_view content, std::string_view kind,
                            const std::optional<std::filesystem::path>& probe,
                            std::string_view dialect = "unknown",
                            std::chrono::milliseconds timeout = std::chrono::seconds(15));
}
