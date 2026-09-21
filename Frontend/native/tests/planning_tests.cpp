// Test-only line-framed adapter for the pure planning policies. It obtains the
// supplied observations through the already verified native readers (store,
// instance inspection, catalog projection) and supplied JSON values; it is
// never added to the production CLI.
#include "c2/frontend/planning.hpp"
#include "c2/frontend/store.hpp"
#include "catalog_internal.hpp"
#include "content_internal.hpp"
#include "discovery_internal.hpp"
#include "manifest_access.hpp"
#include "planning_internal.hpp"
#include "store_paths.hpp"
#include <iostream>
#include <optional>
#include <type_traits>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
using compat::Value;
using compat::Kind;
namespace pi = planning_internal;
static_assert(!std::is_default_constructible_v<planning::Revision>);
static_assert(!std::is_default_constructible_v<planning::Selection>);
static_assert(!std::is_default_constructible_v<planning::StateObservation>);
static_assert(!std::is_default_constructible_v<planning::GenesisPlan>);
static_assert(!std::is_default_constructible_v<planning::LaunchRequest>);
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(std::string("planning typed assertion: ") + what); }
static std::vector<std::u32string> texts(const Value& array) {
    std::vector<std::u32string> out;
    for (const auto& v : array.array) out.push_back(v.string);
    return out;
}
static std::vector<std::u32string> codes(const Value& diagnostics) {
    std::vector<std::u32string> out;
    for (const auto& d : diagnostics.array) out.push_back(d.at(U"code").string);
    return out;
}
static std::vector<std::u32string> strings_of(const Value& array) { return texts(array); }
// Same outcome through the public typed API as through the supplied-value seam.
template <class F> static void same_outcome(const std::string& seam_bytes, const planning::Error* seam_error, F&& typed) {
    try {
        auto bytes = typed();
        require(!seam_error && bytes == seam_bytes, "typed API export differs from the supplied-value result");
    } catch (const planning::Error& e) {
        require(seam_error && std::string(e.what()) == seam_error->what(), "typed API error differs");
    }
}
static void check_plan(const planning::GenesisPlan& plan, const Value& v) {
    require(plan.adapter() == v.at(U"adapter").string && plan.candidate_argv() == texts(v.at(U"candidate_argv")), "plan argv");
    require(plan.score_requirement().decimal == v.at(U"score_requirement").integer && !plan.process_launch_allowed(), "plan score");
    require(plan.license_mask().has_value() == v.contains(U"license_mask") && (!plan.license_mask() || plan.license_mask()->decimal == v.at(U"license_mask").integer), "license mask");
    require(plan.weapon_mask().has_value() == v.contains(U"weapon_mask") && (!plan.weapon_mask() || plan.weapon_mask()->decimal == v.at(U"weapon_mask").integer), "weapon mask");
    require(plan.diagnostic_codes() == codes(v.at(U"diagnostics")), "plan diagnostics");
}
static std::string genesis(const Value& r, bool hunt) {
    auto root = content_internal::native_units(r.at(U"root").string);
    auto hint = r.contains(U"dialect_hint") ? r.at(U"dialect_hint").string : U"unknown";
    std::optional<catalog::Projection> projection{catalog::project(root, hint)};
    const Value &revision = r.at(U"revision"), &slot = r.at(U"slot"), &selection = r.at(U"selection"), &score = r.at(U"score");
    auto evaluate = hunt ? pi::hunt_policy : pi::observer_policy;
    std::string seam_bytes; std::optional<planning::Error> seam_error;
    try { seam_bytes = compat::display(evaluate(revision, *projection, slot, selection, score)); }
    catch (const planning::Error& e) { seam_error.emplace(e); }
    // Public handles: the revision from a manifest instance when one is named,
    // otherwise the supplied value; integer slots/scores through catalog::Integer.
    std::optional<planning::Revision> typed_revision;
    if (r.contains(U"store")) {
        auto instance = get_instance(Store(content_internal::native_units(r.at(U"store").string)).read(), r.at(U"identity").string);
        typed_revision = planning::revision_of(instance);
        require(typed_revision->export_json() == compat::display(revision), "revision_of export");
    } else typed_revision = planning::PlanningAccess::revision(revision);
    auto typed_selection = planning::PlanningAccess::selection(selection);
    if (r.contains(U"typed") && r.at(U"typed").boolean) {
        auto tod = planning::Integer{selection.at(U"time_of_day").integer};
        auto built = hunt ? planning::Selection::hunt(selection.at(U"area").string, strings_of(selection.at(U"licenses")), strings_of(selection.at(U"weapons")), tod)
                          : planning::Selection::observer(selection.at(U"area").string, tod);
        require(built.export_json() == compat::display(selection), "typed selection export");
        typed_selection = built;
    }
    std::optional<planning::Integer> typed_score;
    if (score.kind == Kind::integer) typed_score = planning::Integer{score.integer};
    auto policy = hunt ? planning::hunt_policy : planning::observer_policy;
    if (slot.kind == Kind::integer) {
        std::optional<planning::GenesisPlan> retained;
        same_outcome(seam_bytes, seam_error ? &*seam_error : nullptr, [&] {
            retained.emplace(policy(*typed_revision, *projection, planning::Integer{slot.integer}, typed_selection, typed_score));
            return retained->export_json();
        });
        if (retained) {
            auto copy = *retained;
            projection.reset(); typed_revision.reset();
            require(copy.export_json() == seam_bytes, "retained plan outlives projection and revision");
            check_plan(copy, compat::parse(seam_bytes));
        }
    }
    for (auto bad : {"", "01", "-0", "1.0", " 1", "+1", "x"}) {
        bool threw = false;
        try { (void)planning::Selection::observer(U"areas:0", planning::Integer{bad}); } catch (const std::invalid_argument&) { threw = true; }
        require(threw, "non-canonical Integer accepted");
    }
    if (seam_error) throw *seam_error;
    return seam_bytes;
}
static std::string launch(const Value& r) {
    auto store = content_internal::native_units(r.at(U"store").string);
    auto manifest = Store(store).read();
    const auto& association_id = r.at(U"association").string;
    const Value& associations = ManifestAccess::data(manifest).at(U"associations");
    std::optional<InstanceObservation> instance;
    if (associations.contains(association_id)) instance.emplace(get_instance(manifest, associations.at(association_id).at(U"instance_id").string));
    // An unknown association fails before any observation is consulted; the
    // driver still has to hand over some owned observation.
    DiscoveryObservation observation = instance ? inspect_instance(*instance) : recognize(store);
    const Value& arguments = r.at(U"arguments");
    const auto &id = r.at(U"id").string, &created_at = r.at(U"created_at").string;
    std::string seam_bytes; std::optional<planning::Error> seam_error;
    std::optional<planning::LaunchRequest> stage_one;
    try { stage_one.emplace(pi::begin_launch(manifest, association_id, observation, arguments, id, created_at)); seam_bytes = stage_one->export_json(); }
    catch (const planning::Error& e) { seam_error.emplace(e); }
    if (r.contains(U"typed") && r.at(U"typed").boolean) {
        planning::LaunchSelection selection{arguments.at(U"area").string, strings_of(arguments.at(U"licenses")), strings_of(arguments.at(U"weapons")),
            strings_of(arguments.at(U"equipment")), arguments.at(U"mode").string, planning::Integer{arguments.at(U"time_of_day").integer}};
        same_outcome(seam_bytes, seam_error ? &*seam_error : nullptr, [&] { return planning::begin_launch(manifest, association_id, observation, selection, id, created_at).export_json(); });
    }
    if (seam_error) throw *seam_error;
    require(stage_one->selection_status() == U"blocked" && stage_one->candidate_argv().empty() && !stage_one->process_launch_allowed(), "stage one shape");
    // An unrecognized installation is final here: no projection or state is consulted.
    if (stage_one->complete()) return seam_bytes;
    auto parsed = compat::parse(seam_bytes);
    require(parsed.at(U"capabilities").at(U"installation_recognized").string == U"yes" && !parsed.contains(U"affordability"), "recognized stage one");
    const Value& instance_value = discovery_internal::instance_value(*instance);
    std::optional<catalog::Projection> projection{catalog::project(content_internal::native_units(instance_value.at(U"path").string), instance_value.at(U"dialect_hint").string)};
    auto state = planning::PlanningAccess::state(r.at(U"state"));
    auto result = planning::evaluate_launch(*stage_one, *projection, state);
    require(result.complete() && !result.process_launch_allowed(), "stage two completion");
    bool rejected = false;
    try { (void)planning::evaluate_launch(result, *projection, state); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "complete request evaluated twice");
    auto copy = result;
    projection.reset(); stage_one.reset(); instance.reset();
    auto bytes = copy.export_json();
    auto value = compat::parse(bytes);
    require(copy.selection_status() == value.at(U"selection_status").string && copy.candidate_argv() == texts(value.at(U"candidate_argv")), "stage two typed");
    require(copy.diagnostic_codes() == codes(value.at(U"diagnostics")) && state.export_json() == compat::display(value.at(U"state_observation")), "stage two state");
    return bytes;
}
static std::string run(const Value& r) {
    const auto& op = r.at(U"op").string;
    if (op == U"observer") return genesis(r, false);
    if (op == U"hunt") return genesis(r, true);
    if (op == U"launch") return launch(r);
    throw std::runtime_error("unknown test operation");
}
int main() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string line;
    while (std::getline(std::cin, line)) {
        Value out; out.kind = Kind::object; Value ok; ok.kind = Kind::boolean;
        auto failure = [&](std::string_view kind, const std::exception& e) {
            out.object = {{U"ok", ok}, {U"error", pi::string_value(store_paths::native_points(std::filesystem::path(e.what())))}, {U"kind", pi::ascii_value(kind)}};
        };
        try {
            auto bytes = run(compat::parse(line)); ok.boolean = true;
            out.object = {{U"ok", ok}, {U"value", compat::parse(bytes)}, {U"json", pi::ascii_value(bytes)}};
        } catch (const planning::Error& e) { failure("frontend", e); }
        catch (const std::invalid_argument& e) { failure("type", e); }
        catch (const std::exception& e) { failure("unexpected", e); }
        std::cout << compat::compact(out) << '\n' << std::flush;
    }
}
