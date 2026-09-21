#pragma once
// Pure planning policies (2C.1): the revision-pinned Genesis observer and hunt
// policies (lodge.genesis / lodge.genesis_hunt). Every input is supplied by the
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
GenesisPlan observer_policy(const Revision&, const catalog::Projection&, const Integer& slot,
                            const Selection&, const std::optional<Integer>& score);
GenesisPlan hunt_policy(const Revision&, const catalog::Projection&, const Integer& slot,
                        const Selection&, const std::optional<Integer>& score);

}
