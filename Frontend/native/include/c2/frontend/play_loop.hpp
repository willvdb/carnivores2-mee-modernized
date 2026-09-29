#pragma once
// Typed presentation boundary. All policy, locking, trust and persistence remain
// in the existing backend. No CLI dispatch, JSON input, or implicit migration.
#include "c2/frontend/planning.hpp"
#include "c2/frontend/store.hpp"
#include <atomic>

namespace c2::frontend::play_loop {
struct Diagnostic { std::u32string code, message; };
struct Association {
    std::u32string id, hunter_id, instance_id, origin, ownership, authority;
    std::optional<std::u32string> current_generation;
};
std::vector<Association> associations(const Manifest&);
struct Evidence {
    std::vector<Diagnostic> diagnostics;
    // Complete backend observation for text-only inspection. Never API input.
    std::string details;
};
struct Plan : Evidence { bool process_launch_allowed = false; };
struct NativeObservation {
    std::u32string member;
    std::optional<planning::Integer> score, rank; // raw native values, never normalized
};
struct Session : Evidence {
    std::u32string id, state, association_id;
    std::optional<std::u32string> generation;
    // Missing/null observation differs from an observed empty list.
    std::optional<std::vector<std::u32string>> changed_members;
    std::optional<std::u32string> comparison_status;
    std::optional<std::vector<NativeObservation>> before, after;
};
struct Preview : Evidence {
    bool allowed = false;
    std::u32string session_id, expected_generation, candidate_sha256, status;
};
struct Receipt : Evidence { std::u32string result, current_generation; };
struct Authorization {
    std::filesystem::path engine;
    std::u32string trusted_sha256;
    bool experimental_native_hunt = false;
};
struct Access; // private fixture seam; not defined in the installed API
class Client {
public:
    Client(Store store, std::optional<std::filesystem::path> probe, bool allow_writes = false);
    catalog::Projection catalog(std::u32string_view association) const;
    Plan plan(std::u32string_view association, const planning::Selection&) const;
    Session prepare(std::u32string_view association, const planning::Selection&,
                    const Authorization&, unsigned timeout_seconds = 900) const;
    // Run returns the durable returned/failed journal. Reconcile is explicit.
    // cancel also prevents a launch when set before preflight finishes.
    Session run(std::u32string_view session, const Authorization&, const std::atomic<bool>& cancel) const;
    Session inspect(std::u32string_view session) const;
    Session reconcile(std::u32string_view session) const;
    Preview preview(std::u32string_view session, std::u32string_view expected_generation) const;
    Receipt accept(const Preview&) const;
    Session recover(std::u32string_view session) const;
    Receipt recover_acceptance(std::u32string_view session) const;
    Evidence upgrade() const; // explicit separate action only
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    void require_writes() const;
    friend struct Access;
};
}
