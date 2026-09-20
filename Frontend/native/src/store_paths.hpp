#pragma once
#include "c2/frontend/store.hpp"
namespace c2::frontend::store_paths {
std::vector<std::filesystem::path> ancestor_paths(const std::filesystem::path&);
std::filesystem::path resolve_root(std::filesystem::path);
std::filesystem::path default_directory();
// nullopt only for missing file, after ancestor/final safety checks.
std::optional<std::string> read(const std::filesystem::path&, const ReadPolicy&);
}
