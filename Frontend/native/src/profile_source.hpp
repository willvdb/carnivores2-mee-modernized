#pragma once
// Private source-profile reader, separate from managed capture policy.
#include <filesystem>
#include <string>
namespace c2::frontend::profile_source {
std::string read_member(const std::filesystem::path& canonical_root,
                        const std::filesystem::path& relative);
}
