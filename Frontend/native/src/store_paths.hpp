#pragma once
#include "c2/frontend/store.hpp"
namespace c2::frontend::store_paths {
// Native names use Python's filesystem decoding (UTF-8 surrogateescape on POSIX).
std::u32string native_points(const std::filesystem::path&);
void safe_path(const std::filesystem::path&);
bool resolves_to_self(const std::filesystem::path&);
bool windows_path_equal(std::u32string, std::u32string);
std::vector<std::filesystem::path> ancestor_paths(const std::filesystem::path&);
// Path.resolve semantics without Store tilde expansion or safety policy.
std::filesystem::path resolve_native(const std::filesystem::path&);
std::filesystem::path expand_user(std::filesystem::path);
std::filesystem::path resolve_root(std::filesystem::path);
std::filesystem::path default_directory();
// nullopt only for missing file, after ancestor/final safety checks.
std::optional<std::string> read(const std::filesystem::path&, const ReadPolicy&, bool require_eof = false);
}
