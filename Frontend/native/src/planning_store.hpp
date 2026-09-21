#pragma once
// PRIVATE store-backed planning (slice 2C.2, partial): the StateObservation
// producer profiles.refresh_association and the refresh-state write wrapper.
// The launch-dry-run, genesis-observer-plan and native-hunt plan wrappers are
// NOT implemented here yet.
#include "c2/frontend/store.hpp"
#include "json_compat.hpp"
#include "store_write.hpp"
#include <optional>
namespace c2::frontend::planning_store {
// An association whose authority is 'managed-state-history' needs generation
// resolution over the mutable manifest value, which is not wired yet. This is
// thrown instead of guessing a root or silently selecting a generation.
class NotImplemented : public std::logic_error { public: using std::logic_error::logic_error; };
// profiles.refresh_association: observes through the configured helper and
// sets association['last_observation'] on the supplied manifest value.
// Returns the observation. Read-only with respect to the filesystem.
compat::Value refresh_association(const Store&, compat::Value& data, std::u32string_view identity,
                                  const std::optional<std::filesystem::path>& probe);
// CLI refresh-state: the same observation inside Store.transaction (lock,
// revalidation, backup, write only when the sorted manifest changed).
compat::Value refresh_state(const Store&, std::u32string_view identity,
                            const std::optional<std::filesystem::path>& probe,
                            const store_write::FailureHook& hook = {});
}
