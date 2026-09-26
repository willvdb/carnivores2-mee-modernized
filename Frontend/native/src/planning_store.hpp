#pragma once
// PRIVATE store-backed planning (slice 2C.2). No production CLI exposure.
#include "c2/frontend/planning.hpp"
#include "c2/frontend/store.hpp"
#include "json_compat.hpp"
#include "store_write.hpp"
#include <optional>
namespace c2::frontend::planning_store {
struct PinSnapshot {
    compat::Value pins;
    std::vector<CapturedBlob> blobs;
};
// sessions.snapshot_pins. Caller holds any operation-specific lock (as in
// Python); this read-only operation never writes a workspace or native bytes.
PinSnapshot snapshot_pins(const Store&, std::u32string_view association_id,
    const compat::Value& selection, const std::optional<std::filesystem::path>& probe,
    const std::optional<compat::Value>& expected_codec = std::nullopt,
    std::u32string_view mode = U"observer", bool managed = false,
    const std::optional<std::u32string>& generation = std::nullopt);
// The optional policy seam is test-only, matching the existing Python
// asset-free policy doubles. Production always uses the pinned pure policies.
using PolicyEvaluator = std::function<compat::Value(const compat::Value&, const catalog::Projection&,
    const compat::Value&, const compat::Value&, const compat::Value&)>;
compat::Value plan_observer(const Store&, std::u32string area, std::u32string_view association_id,
    const planning::Integer& time_of_day, const std::optional<std::filesystem::path>& probe,
    const std::optional<std::filesystem::path>& engine = std::nullopt,
    const PolicyEvaluator& test_policy = {});
compat::Value plan_hunt(const Store&, std::u32string_view association_id,
    const compat::Value& selection, const std::optional<std::filesystem::path>& probe,
    const PolicyEvaluator& test_policy = {});
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
// CLI launch-dry-run (lodge.launch.prepare inside Store.transaction). Both
// planning stages run here from the one association under the writer lock, so
// the projection and state handed to evaluate_launch always belong to the
// stage-one instance and complete() is handled internally (PR #26 follow-up).
// A recognized installation refreshes last_observation; an unrecognized one
// returns the final stage-one request and writes nothing. Planning only:
// process_launch_allowed stays false and nothing is authorized or accepted.
planning::LaunchRequest launch_dry_run(const Store&, std::u32string_view association_id,
                                       const planning::LaunchSelection&,
                                       const std::optional<std::filesystem::path>& probe,
                                       const store_write::FailureHook& hook = {});
}
