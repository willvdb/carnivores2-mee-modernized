#pragma once
// Pure planning policies (2C.1): the revision-pinned Genesis observer and hunt
// policies (lodge.genesis / lodge.genesis_hunt) and the evaluation core of the
// generic launch dry-run (lodge.launch.prepare). Every input is supplied by the
// caller as an owned observation; nothing here reads the filesystem, takes the
// store lock, snapshots or captures pins, executes the codec helper or an
// engine, generates identities or timestamps, or writes the manifest. The
// reference wrappers (plan_observer, native-hunt plan, launch-dry-run, and
// refresh_association's `last_observation` write) belong to 2C.2 after 3A.
#include "c2/frontend/catalog.hpp"
#include "c2/frontend/discovery.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace c2::frontend::planning {
// lodge.store.FrontendError as the reference policies raise it (exact messages).
class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
// Canonical arbitrary-magnitude decimal (catalog::Integer); never int64/binary64.
using catalog::Integer;

// Pinned constants. The revision equality is Python dict equality over the
// supplied manifest value (numeric equality, exact key set), not a hash check.
inline constexpr std::u32string_view OBSERVER_POLICY_ID = U"genesis-current-mee-observer-v1";
inline constexpr std::u32string_view HUNT_POLICY_ID = U"genesis-current-mee-hunt-v1";
inline constexpr std::u32string_view SCORE_MODIFIERS = U"smod=0.85,0.70,0.80,1.0,1.25,1.0";
struct PinnedRevision { std::u32string_view algorithm, sha256; std::size_t file_count; std::uint64_t byte_count; };
inline constexpr PinnedRevision GENESIS_REVISION{U"huntdat-sha256-v1",
    U"9c6fc5221744ad8e9a74689d308ba572b6aefe6cd6c317e030e5774757c2bf65", 344, 768073101};
struct ExpectedCounts { std::size_t areas, licenses, weapons, equipment, physical_maps; };
inline constexpr ExpectedCounts EXPECTED_COUNTS{8, 9, 8, 4, 9};

// Retained manifest content revision (instance['revision'], the value the
// reference pins from the store), including any retained unknown keys.
class Revision {
public:
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Revision(std::shared_ptr<const Impl>);
    friend struct PlanningAccess;
};
Revision revision_of(const InstanceObservation&);

// Policy selection dictionary. The typed constructors reproduce the reference
// callers' key orders: observer as plan_observer/prepare_native build it, hunt
// as the native-hunt CLI builds it. Journal-pinned selections with other
// retained kinds arrive through the 2C.2 wrappers, not through this API.
class Selection {
public:
    static Selection observer(std::u32string area, Integer time_of_day);
    static Selection hunt(std::u32string area, std::vector<std::u32string> licenses,
                          std::vector<std::u32string> weapons, Integer time_of_day);
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Selection(std::shared_ptr<const Impl>);
    friend struct PlanningAccess;
};

// Immutable observer_policy / hunt_policy result. export_json is the reference
// dictionary in its key order; the slot and selection are echoed as supplied.
class GenesisPlan {
public:
    const std::u32string& adapter() const noexcept;
    std::vector<std::u32string> candidate_argv() const;
    Integer score_requirement() const;
    std::optional<Integer> license_mask() const; // hunt only
    std::optional<Integer> weapon_mask() const;  // hunt only
    bool process_launch_allowed() const noexcept; // always false
    std::vector<std::u32string> diagnostic_codes() const;
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit GenesisPlan(std::shared_ptr<const Impl>);
    friend struct PlanningAccess;
};
// The score is the decoded native save score; nullopt means the observation
// is not an exact integer (None, bool, float or text in the reference), which
// every policy refuses with the same message. Slot is the association's
// filename_slot. Violations raise Error with the reference message; the
// caller must have obtained a fresh full fingerprint for the revision.
// Representation limits of this typed API (no coercion is invented):
//  - Selection lists are sequences of text only. The reference distinguishes
//    a tuple from a list and accepts arbitrary element kinds; neither is
//    representable here, so those reference branches are reachable only
//    through the private supplied-value seam used by the oracle tests.
//  - A non-finite or fractional score (NaN, +/-Infinity, 1.5) cannot be passed
//    as an Integer. It is nullopt, which refuses exactly as the reference
//    refuses every non-exact-int score; it is never rounded or clamped.
GenesisPlan observer_policy(const Revision&, const catalog::Projection&, const Integer& slot,
                            const Selection&, const std::optional<Integer>& score);
GenesisPlan hunt_policy(const Revision&, const catalog::Projection&, const Integer& slot,
                        const Selection&, const std::optional<Integer>& score);

// lodge.launch.prepare arguments (CLI --area/--license/--weapon/--equipment/--mode/--time).
struct LaunchSelection {
    std::u32string area;
    std::vector<std::u32string> licenses, weapons, equipment;
    std::u32string mode = U"hunt";
    Integer time_of_day{"1"};
};
// The association state observation exactly as refresh_association returns it
// (status, diagnostics, inspected files with decoded saves, or missing-state).
// 2C.1 only consumes it; its native producer is the 2C.2 refresh wrapper.
class StateObservation {
public:
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit StateObservation(std::shared_ptr<const Impl>);
    friend struct PlanningAccess;
};
// Immutable launch dry-run request. complete() is false only for a recognized
// stage-one request, which evaluate_launch completes.
class LaunchRequest {
public:
    bool complete() const noexcept;
    const std::u32string& selection_status() const noexcept;
    std::vector<std::u32string> candidate_argv() const;
    std::vector<std::u32string> diagnostic_codes() const;
    bool process_launch_allowed() const noexcept; // always false
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit LaunchRequest(std::shared_ptr<const Impl>);
    friend struct PlanningAccess;
};
// Stage one: manifest lookups plus the supplied installation observation of the
// association's instance, with the caller's request id and creation time. When
// the installation is not recognized the request is final; the reference never
// projects the catalog or refreshes state on that path, so neither is taken.
LaunchRequest begin_launch(const Manifest&, std::u32string_view association_id,
                           const DiscoveryObservation& observation, const LaunchSelection&,
                           std::u32string_view id, std::u32string_view created_at);
// Stage two over the supplied catalog projection of the instance and the
// refreshed association state. A complete request throws std::invalid_argument.
LaunchRequest evaluate_launch(const LaunchRequest&, const catalog::Projection&, const StateObservation&);
}
