#pragma once
// Private observation/test seams. No production JSON or metadata trust inputs.
#include "c2/frontend/content.hpp"
#include "json_compat.hpp"
#include <vector>
#include <functional>
namespace c2::frontend::content_internal {
// Shared source semantics, deliberately separate from managed-store safety.
std::filesystem::path source_spelling(const std::filesystem::path&);
std::filesystem::path source_syscall(const std::filesystem::path&);
std::filesystem::file_status source_status(const std::filesystem::path&);
bool source_is_link(const std::filesystem::path&);
// Actual bytes up to limit, follows ordinary aliases; owned regular handle only.
std::string read_prefix(const std::filesystem::path&, std::size_t limit);
std::filesystem::path native_units(std::u32string_view);
std::vector<std::filesystem::path> walk_files(const std::filesystem::path&);
struct Member {
    std::u32string relative;
    std::string size, modified, inode;
    bool operator==(const Member& b) const { return relative == b.relative && size == b.size && modified == b.modified && inode == b.inode; }
};
std::vector<Member> content_inventory(const std::filesystem::path&);
std::string hash_file(const std::filesystem::path&);
// Test synchronization only. Production callers omit the callback; phases
// expose no metadata/bytes and cannot inject trusted observations.
enum class FingerprintPhase { initial_inventory, member_hashed, final_inventory };
std::string fingerprint_payload(const std::filesystem::path&,
    const std::function<void(FingerprintPhase)>& phase = {});
compat::Value inventory_value(const std::vector<Member>&);
}
