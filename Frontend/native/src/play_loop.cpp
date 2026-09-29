#include "c2/frontend/play_loop.hpp"
#include "play_loop_internal.hpp"
#include "manifest_access.hpp"
#include "planning_internal.hpp"
#include "session_journal.hpp"
#include "session_runner.hpp"
#include "acceptance.hpp"
#include "store_ops.hpp"

namespace c2::frontend::play_loop {
namespace {
using compat::Value;
std::u32string text(const Value& v, std::u32string_view key) {
    if (!v.contains(key) || v.at(key).kind != compat::Kind::string) return {};
    return v.at(key).string;
}
Evidence evidence(const Value& v) {
    Evidence e;
    e.details = compat::display(v);
    if (v.contains(U"diagnostics"))
        for (const auto& d : v.at(U"diagnostics").array)
            e.diagnostics.push_back({text(d, U"code"), text(d, U"message")});
    return e;
}
Session session(const Value& v) {
    Session s;
    static_cast<Evidence&>(s) = evidence(v);
    s.id = text(v, U"id"); s.state = text(v, U"state");
    if (v.contains(U"pins")) {
        const auto& pins = v.at(U"pins");
        s.association_id = text(pins, U"association_id");
        auto g = text(pins, U"generation_id");
        if (!g.empty()) s.generation = g;
    }
    if (v.contains(U"reconciliation")) {
        const auto& r = v.at(U"reconciliation");
        auto status = text(r, U"comparison_status");
        if (!status.empty()) s.comparison_status = status;
        if (r.contains(U"changed_members") && r.at(U"changed_members").kind == compat::Kind::array) {
            s.changed_members.emplace();
            for (const auto& m : r.at(U"changed_members").array) s.changed_members->push_back(m.string);
        }
    }
    return s;
}
Receipt receipt(const Value& v) {
    Receipt r; static_cast<Evidence&>(r) = evidence(v);
    r.result = text(v, U"result"); r.current_generation = text(v, U"current_generation"); return r;
}
}
std::vector<Association> associations(const Manifest& manifest) {
    std::vector<Association> out;
    for (const auto& entry : ManifestAccess::data(manifest).at(U"associations").object) {
        const auto& v = entry.second;
        Association a{entry.first, text(v,U"hunter_id"),text(v,U"instance_id"),text(v,U"origin"),
                      text(v,U"ownership"),text(v,U"authority"),std::nullopt};
        if (v.contains(U"managed_state")) {
            auto g = text(v.at(U"managed_state"), U"current_generation");
            if (!g.empty()) a.current_generation = g;
        }
        out.push_back(std::move(a));
    }
    return out;
}
Client::Client(Store store, std::optional<std::filesystem::path> probe, bool writes)
    : impl_(std::make_shared<Impl>(std::move(store), std::move(probe), writes)) {}
void Client::require_writes() const {
    if (!impl_->writes) throw StoreError("store is read-only; restart with --allow-writes for explicit mutations");
    // A supplied missing store must never be implicitly created.
    if (!std::filesystem::is_regular_file(impl_->store.directory() / "lodge.json"))
        throw StoreError("an existing lodge.json is required");
}
Plan Client::plan(std::u32string_view a, const planning::Selection& selection) const {
    auto v = planning_store::plan_hunt(impl_->store,a,planning::PlanningAccess::value(selection),impl_->probe,impl_->policies.hunt);
    Plan p; static_cast<Evidence&>(p) = evidence(v); return p;
}
Session Client::prepare(std::u32string_view a, const planning::Selection& selection, const Authorization& auth, unsigned timeout) const {
    require_writes();
    return session(native_session::prepare(native_session::preparation_adapter(impl_->store),impl_->store,a,
        planning::PlanningAccess::value(selection),auth.engine,planning_internal::string_value(auth.trusted_sha256),
        auth.experimental_native_hunt,planning_internal::integer_value(std::to_string(timeout)),impl_->probe,impl_->policies));
}
Session Client::run(std::u32string_view id, const Authorization& auth, const std::atomic<bool>& cancel) const {
    require_writes();
    return session(session_runner::run_native(std::nullopt,true,impl_->store,id,auth.engine,
        planning_internal::string_value(auth.trusted_sha256),auth.experimental_native_hunt,impl_->probe,&cancel,impl_->policies,&cancel));
}
Session Client::inspect(std::u32string_view id) const { return session(session_journal::read(impl_->store,id)); }
Session Client::reconcile(std::u32string_view id) const {
    require_writes(); return session(reconciliation::reconcile_session(impl_->store,id,impl_->probe,impl_->policies));
}
Preview Client::preview(std::u32string_view id, std::u32string_view generation) const {
    auto v = acceptance::preview_acceptance(impl_->store,id,generation,impl_->probe,impl_->policies);
    Preview p; static_cast<Evidence&>(p) = evidence(v);
    p.allowed = v.at(U"allowed").boolean; p.session_id = text(v,U"session_id");
    p.expected_generation = text(v,U"expected_generation"); p.candidate_sha256 = text(v,U"candidate_sha256");
    p.status = text(v,U"status"); return p;
}
Receipt Client::accept(const Preview& p) const {
    require_writes();
    if (!p.allowed || p.candidate_sha256.empty() || p.expected_generation.empty() || p.session_id.empty())
        throw StoreError("acceptance requires an allowed preview with session, generation and candidate digest");
    return receipt(acceptance::accept_candidate(impl_->store,p.session_id,p.expected_generation,p.candidate_sha256,impl_->probe,impl_->policies));
}
Session Client::recover(std::u32string_view id) const {
    require_writes(); return session(session_runner::recover_session(impl_->store,id,impl_->probe,impl_->policies));
}
Receipt Client::recover_acceptance(std::u32string_view id) const {
    require_writes(); return receipt(acceptance::recover_acceptance(impl_->store,id));
}
Evidence Client::upgrade() const { require_writes(); return evidence(store_ops::upgrade_store(impl_->store)); }
}
