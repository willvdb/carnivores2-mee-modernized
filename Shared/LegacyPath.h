#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

// Existing-content lookup only. No I/O handles, engine state or write policy.
namespace LegacyPath {
enum class Error { None, InvalidPath, UnsupportedRoot, Missing, Ambiguous, Filesystem };

struct Result {
    std::filesystem::path path; // Empty on failure; actual spelling on success.
    Error error = Error::None;
    std::string requested;
    std::filesystem::path directory;
    std::filesystem::path component;
    std::error_code systemError;

    explicit operator bool() const { return error == Error::None; }
    std::string Message() const;
};

// Both slashes separate components. Exact spelling wins, otherwise require a
// unique ASCII case-insensitive sibling. Root is a native path, used only for
// relative requests; the default preserves game working-directory semantics.
// Keep dot components (notably symlink/..) and never canonicalize or cache.
// Windows drive/UNC paths are supported by the Windows filesystem library only.
Result Resolve(std::string_view logical, const std::filesystem::path& root = ".");
}
