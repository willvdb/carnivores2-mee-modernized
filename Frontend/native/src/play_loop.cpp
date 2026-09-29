#include "c2/frontend/play_loop.hpp"
#include "play_loop_internal.hpp"
#include "manifest_access.hpp"
#include "planning_internal.hpp"
#include "session_journal.hpp"
#include "session_runner.hpp"
#include "acceptance.hpp"
#include "store_ops.hpp"
#include "content_internal.hpp"
#include "schema_compat.hpp"
#include <algorithm>

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
std::optional<std::vector<NativeObservation>> observations(const Value& v, std::u32string_view key) {
    if (!v.contains(key) || v.at(key).kind != compat::Kind::object) return std::nullopt;
    std::vector<NativeObservation> out;
    for (const auto& member : v.at(key).object) {
        NativeObservation o; o.member = member.first;
        auto number = [&](std::u32string_view name) -> std::optional<planning::Integer> {
            if (!member.second.contains(name) || member.second.at(name).kind != compat::Kind::integer) return std::nullopt;
            return planning::Integer{member.second.at(name).integer};
        };
        o.score = number(U"score"); o.rank = number(U"rank"); out.push_back(std::move(o));
    }
    return out;
}
Session session(const Value& v) {
    Session s;
    static_cast<Evidence&>(s) = evidence(v);
    s.id = text(v, U"id"); s.state = text(v, U"state");
    if (v.contains(U"pins")) {
        const auto& pins = v.at(U"pins");
        s.association_id = text(pins, U"association_id");
        s.before = observations(pins, U"source_observation");
        auto g = text(pins, U"generation_id");
        if (!g.empty()) s.generation = g;
    }
    s.after = observations(v, U"returned_observation");
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
struct LoadoutContext::Impl {
    catalog::Projection catalog;
    Value revision, slot, score;
    std::optional<std::u32string> generation;
    planning_store::PolicyEvaluator policy, partial_policy;
};
const catalog::Projection& LoadoutContext::catalog() const { return impl_->catalog; }
const std::optional<std::u32string>& LoadoutContext::generation() const { return impl_->generation; }
std::u32string LoadoutContext::label(catalog::Group group, std::u32string_view id) const {
    // Semantic names belong to this bounded adapter, never to ordinal widget callbacks.
    if(group==catalog::Group::equipment && impl_->revision.contains(U"sha256") &&
       impl_->revision.at(U"sha256").string==planning::GENESIS_REVISION.sha256) {
        const std::pair<std::u32string_view,std::u32string_view> names[]={{U"equipment:0",U"Camouflage"},{U"equipment:1",U"Radar"},
            {U"equipment:2",U"Cover scent"},{U"equipment:3",U"Double ammo"}};
        for(const auto& n:names) if(n.first==id) return std::u32string(n.second);
    }
    for(const auto& entry:impl_->catalog.entries(group)) if(entry.id()==id) {
        if(auto l=entry.label()) if(auto text=std::get_if<std::u32string>(&*l)) return *text;
    }
    return std::u32string(id);
}
LoadoutAdvice LoadoutContext::evaluate(const planning::Selection& selection) const {
    LoadoutAdvice advice;
    if (impl_->score.kind == compat::Kind::integer) advice.score = planning::Integer{impl_->score.integer};
    try {
        const auto result = impl_->policy(impl_->revision, impl_->catalog, impl_->slot,
                                         planning::PlanningAccess::value(selection), impl_->score);
        advice.allowed = true;
        if (result.contains(U"score_requirement"))
            advice.requirement = planning::Integer{result.at(U"score_requirement").integer};
    } catch (const std::exception& e) { advice.diagnostic = e.what(); }
    // Ask the same policy for the requirement even when the real score is too low.
    // This is an observation, never eligibility or launch authorization.
    if (!advice.requirement) try {
        const auto quote = impl_->partial_policy(impl_->revision, impl_->catalog, impl_->slot,
            planning::PlanningAccess::value(selection), planning_internal::integer_value("2147483647"));
        if (quote.contains(U"score_requirement")) advice.requirement = planning::Integer{quote.at(U"score_requirement").integer};
    } catch (const std::exception&) {}
    if (advice.score && advice.requirement &&
        planning_internal::compare_decimal(advice.score->decimal,"0") >= 0 &&
        planning_internal::compare_decimal(advice.score->decimal,"2147483647") <= 0 &&
        planning_internal::compare_decimal(advice.requirement->decimal,"0") >= 0 &&
        planning_internal::compare_decimal(advice.requirement->decimal,"2147483647") <= 0)
        advice.remaining = planning::Integer{std::to_string(std::stoll(advice.score->decimal)-std::stoll(advice.requirement->decimal))};
    return advice;
}
LoadoutAdvice LoadoutContext::alternative(const planning::Selection& selection, catalog::Group group, std::u32string id) const {
    const auto chosen=selection.selected(group);
    const bool removing=group!=catalog::Group::areas && std::find(chosen.begin(),chosen.end(),id)!=chosen.end();
    const auto next=selection.with(group,std::move(id));
    auto advice=evaluate(next);
    try {
        impl_->partial_policy(impl_->revision,impl_->catalog,impl_->slot,planning::PlanningAccess::value(next),impl_->score);
        advice.can_change=true;
    } catch(const std::exception& e) { advice.diagnostic=e.what(); }
    if(removing) { advice.can_change=true; advice.diagnostic.clear(); }
    else if(advice.can_change) advice.diagnostic.clear();
    return advice;
}
LoadoutContext Client::loadout_context(std::u32string_view id) const {
    // One immutable manifest view; refresh_association changes only this local copy.
    // No HUNTDAT fingerprint here: advice is deliberately not launch evidence.
    auto data = ManifestAccess::data(impl_->store.read());
    const auto association = data.at(U"associations").at(id);
    if (text(association,U"ownership") != U"managed" || text(association,U"origin") != U"personal")
        throw StoreError("loadout advice requires managed personal state");
    const auto& hunter = data.at(U"hunters").at(association.at(U"hunter_id").string);
    if (hunter.contains(U"archived_at") && schema::truth(hunter.at(U"archived_at"))) throw StoreError("archived hunter cannot start a session");
    const auto instance = data.at(U"instances").at(association.at(U"instance_id").string);
    auto projection = catalog::project(content_internal::native_units(text(instance,U"path")),text(instance,U"dialect_hint"));
    const auto observed = planning_store::refresh_association(impl_->store,data,id,impl_->probe);
    if (text(observed,U"status") != U"unchanged-state") throw StoreError("managed source changed or is unavailable");
    const auto& files = observed.at(U"files");
    if (files.array.size() != 2) throw StoreError("hunt requires a complete SAV/SAB pair");
    for (const auto& diagnostic : observed.at(U"diagnostics").array) {
        const auto code=text(diagnostic,U"code");
        if (code!=U"ownership-unproven" && code!=U"pair-coherence-unverified")
            throw StoreError("native source requires review: " + compat::compact(diagnostic));
    }
    Value score;
    for (const auto& file : files.array) {
        const auto& decoded = file.at(U"decoded");
        if (!decoded.at(U"codec_roundtrip_exact").boolean) throw StoreError("native source is unreadable");
        if (decoded.contains(U"score")) score = decoded.at(U"score");
    }
    std::optional<std::u32string> generation;
    if (association.contains(U"managed_state")) generation = text(association.at(U"managed_state"),U"current_generation");
    auto context = std::make_shared<LoadoutContext::Impl>(LoadoutContext::Impl{
        std::move(projection),instance.at(U"revision"),association.at(U"filename_slot"),score,generation,
        impl_->policies.hunt ? impl_->policies.hunt : planning_store::PolicyEvaluator([](const auto& r,const auto& c,const auto& sl,const auto& se,const auto& sc) {
            return planning_internal::expanded_hunt_policy(r,c,sl,se,sc);
        }),
        impl_->policies.hunt_advice ? impl_->policies.hunt_advice : impl_->policies.hunt ? impl_->policies.hunt : planning_store::PolicyEvaluator([](const auto& r,const auto& c,const auto& sl,const auto& se,const auto& sc) {
            return planning_internal::expanded_hunt_policy(r,c,sl,se,sc,true);
        })});
    return LoadoutContext(std::move(context));
}
Client::Client(Store store, std::optional<std::filesystem::path> probe, bool writes)
    : impl_(std::make_shared<Impl>(std::move(store), std::move(probe), writes)) {}
void Client::require_writes() const {
    if (!impl_->writes) throw StoreError("store is read-only; restart with --allow-writes for explicit mutations");
    // A supplied missing store must never be implicitly created.
    if (!std::filesystem::is_regular_file(impl_->store.directory() / "lodge.json"))
        throw StoreError("an existing lodge.json is required");
}
catalog::Projection Client::catalog(std::u32string_view id) const {
    auto manifest = impl_->store.read();
    const auto& data = ManifestAccess::data(manifest);
    const auto& a = data.at(U"associations").at(id);
    const auto& i = data.at(U"instances").at(a.at(U"instance_id").string);
    return catalog::project(content_internal::native_units(i.at(U"path").string),i.at(U"dialect_hint").string);
}
Plan Client::plan(std::u32string_view a, const planning::Selection& selection) const {
    auto v = planning_store::plan_hunt(impl_->store,a,planning::PlanningAccess::value(selection),impl_->probe,impl_->policies.hunt);
    Plan p; static_cast<Evidence&>(p) = evidence(v);
    p.process_launch_allowed = v.at(U"process_launch_allowed").boolean; return p;
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
