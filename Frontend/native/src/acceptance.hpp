#pragma once
// PRIVATE lodge.acceptance (workstream C, 6A/6B). Commit point: lodge.json
// atomically publishes head + receipt after the independent snapshot.
#include "session_policy.hpp"
#include "store_write.hpp"
namespace c2::frontend::acceptance {
using compat::Value;
std::string candidate_digest(const Value& journal); // sha256(JournalEvidenceV1(journal))
Value preview_acceptance(const Store&, std::u32string_view identity, std::u32string_view expected_generation,
                         const std::optional<std::filesystem::path>& probe, const session_policy::Policies&);
Value accept_candidate(const Store&, std::u32string_view identity, std::u32string_view expected_generation,
                       std::u32string_view expected_candidate_sha256, const std::optional<std::filesystem::path>& probe,
                       const session_policy::Policies&, const store_write::FailureHook& hook = {});
Value recover_acceptance(const Store&, std::u32string_view identity, const store_write::FailureHook& hook = {});
}
