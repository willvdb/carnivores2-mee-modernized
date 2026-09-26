#pragma once
// PRIVATE journal component of lodge.session_io (slice 3B): validation of
// every historical journal version (1-4), the canonical encoder, bounded
// persistence over the 3A atomic write, bounded reading and the state
// machine. No preparation, process execution, reconciliation or recovery.
// Errors are StoreError carrying the reference FrontendError messages.
#include "c2/frontend/store.hpp"
#include "json_compat.hpp"
#include "store_write.hpp"
#include <string_view>
#include <utility>
#include <vector>
namespace c2::frontend::session_journal {
constexpr std::size_t max_journal_bytes = 4u * 1024u * 1024u;
// os.name of this build: "posix" or "nt". A journal of the other flavor is foreign.
std::u32string_view path_flavor() noexcept;
// TRANSITIONS membership; false for an unknown source or target.
bool known_state(std::u32string_view) noexcept;
bool transition_allowed(std::u32string_view from, std::u32string_view to) noexcept;
// validate_journal in the reference check and message order. Never upgrades,
// repairs or normalizes; the value is only inspected.
void validate(const compat::Value& journal, std::u32string_view identity);
// json.dumps(indent=2, sort_keys=True, allow_nan=False) + LF. A non-finite
// float maps to StoreError (reference bare ValueError), writing nothing.
std::string encode(const compat::Value& journal);
// persist: safe root, validation against the root's name, size bound, atomic write.
void persist(const std::filesystem::path& root, const compat::Value& journal,
             const store_write::FailureHook& hook = {});
// read_journal: bounded, duplicate keys refused, then validated. Read and
// decode failures use the reference prefix with a native detail text.
compat::Value read(const Store&, std::u32string_view identity);
// transition: refuses an illegal edge, applies fields then state, appends the
// event, persists, and only then replaces the caller's journal.
using Fields = std::vector<std::pair<std::u32string, compat::Value>>;
void transition(const std::filesystem::path& root, compat::Value& journal, std::string_view state,
                const Fields& fields = {}, const store_write::FailureHook& hook = {});
}
