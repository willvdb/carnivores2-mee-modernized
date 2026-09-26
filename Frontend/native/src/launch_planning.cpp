// Generic launch dry-run evaluation core (lodge.launch.prepare) over supplied
// observations. Stage one uses the manifest and the installation observation;
// stage two adds the catalog projection and the refreshed association state.
#include "c2/frontend/planning.hpp"
#include "planning_internal.hpp"
#include "catalog_internal.hpp"
#include "discovery_internal.hpp"
#include "manifest_access.hpp"
#include "schema_compat.hpp"
#include <algorithm>
#include <map>

namespace c2::frontend::planning {
using compat::Kind;
using compat::Value;
namespace pi = planning_internal;
namespace {
std::u32string digits(std::size_t n) { auto s = std::to_string(n); return {s.begin(), s.end()}; }
Value diagnostic(std::u32string_view code, std::u32string_view message) { return pi::diagnostic_value(code, message); }
Value diagnostic(std::u32string_view code, std::u32string_view message, std::u32string_view key, Value context) {
    auto d = pi::diagnostic_value(code, message);
    d.object.emplace_back(std::u32string(key), std::move(context));
    return d;
}
Value& field(Value& object, std::u32string_view key) {
    for (auto& kv : object.object) if (kv.first == key) return kv.second;
    throw std::logic_error("missing request field");
}
// dict.update: existing keys are replaced in place, new keys appended.
void update(Value& target, const Value& source) {
    for (const auto& kv : source.object) {
        if (target.contains(kv.first)) field(target, kv.first) = kv.second;
        else target.object.emplace_back(kv.first, kv.second);
    }
}
// list(x): a list copies, a str yields its characters, a dict its keys;
// anything else is not iterable (TypeError in the reference).
Value as_list(const Value& v) {
    auto out = pi::array_value();
    if (v.kind == Kind::array) out.array = v.array;
    else if (v.kind == Kind::string) for (auto c : v.string) out.array.push_back(pi::string_value(std::u32string(1, c)));
    else if (v.kind == Kind::object) for (const auto& kv : v.object) out.array.push_back(pi::string_value(kv.first));
    else throw std::invalid_argument("argument is not iterable");
    return out;
}
bool hashable(const Value& v) { return v.kind != Kind::array && v.kind != Kind::object; }
// len(set(items)) over hashable elements with Python equality.
std::size_t distinct(const Value& items) {
    std::vector<const Value*> seen;
    for (const auto& item : items.array) {
        if (!hashable(item)) throw std::invalid_argument("unhashable selection element");
        if (std::none_of(seen.begin(), seen.end(), [&](const Value* s) { return schema::equal(*s, item); })) seen.push_back(&item);
    }
    return seen.size();
}
// `text in container` for a str left operand: list/tuple elements by equality,
// dict keys, or substring of a str; other kinds are not containers.
bool contains_text(const Value& container, const Value& text) {
    if (container.kind == Kind::array) return std::any_of(container.array.begin(), container.array.end(), [&](const Value& v) { return schema::equal(v, text); });
    if (container.kind == Kind::object) return container.contains(text.string);
    if (container.kind == Kind::string) return container.string.find(text.string) != std::u32string::npos;
    throw std::invalid_argument("argument of type is not iterable");
}
const Value& mapping(const Value& v, const char* what) {
    if (v.kind != Kind::object) throw std::invalid_argument(what);
    return v;
}
const Value* get(const Value& object, std::u32string_view key) { return object.contains(key) ? &object.at(key) : nullptr; }
bool exact_int(const Value& v) { return v.kind == Kind::integer; }
bool time_of_day_ok(const Value& v) { return exact_int(v) && (v.integer == "0" || v.integer == "1" || v.integer == "2"); }
bool has_code(const Value& diagnostics, std::initializer_list<std::u32string_view> codes) {
    for (const auto& d : diagnostics.array) {
        const auto& code = d.at(U"code");
        for (auto c : codes) if (pi::is_text(code, c)) return true;
    }
    return false;
}
std::u32string decimal_text(const std::string& decimal) { return {decimal.begin(), decimal.end()}; }
Value strings(const std::vector<std::u32string>& items) {
    auto v = pi::array_value();
    for (const auto& s : items) v.array.push_back(pi::string_value(s));
    return v;
}
Value arguments_value(const LaunchSelection& s) {
    if (!pi::canonical_decimal(s.time_of_day.decimal)) throw std::invalid_argument("Integer is not a canonical decimal");
    auto v = pi::object_value();
    v.object = {{U"area", pi::string_value(s.area)}, {U"licenses", strings(s.licenses)}, {U"weapons", strings(s.weapons)},
        {U"equipment", strings(s.equipment)}, {U"mode", pi::string_value(s.mode)}, {U"time_of_day", pi::integer_value(s.time_of_day.decimal)}};
    return v;
}
std::vector<std::u32string> texts(const Value& array) {
    std::vector<std::u32string> out;
    for (const auto& v : array.array) out.push_back(v.string);
    return out;
}
}
StateObservation::StateObservation(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
std::string StateObservation::export_json() const { return compat::display(impl_->value); }
LaunchRequest::LaunchRequest(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
bool LaunchRequest::complete() const noexcept { return impl_->complete; }
const std::u32string& LaunchRequest::selection_status() const noexcept { return impl_->request.at(U"selection_status").string; }
std::vector<std::u32string> LaunchRequest::candidate_argv() const { return texts(impl_->request.at(U"candidate_argv")); }
std::vector<std::u32string> LaunchRequest::diagnostic_codes() const {
    std::vector<std::u32string> out;
    for (const auto& d : impl_->request.at(U"diagnostics").array) out.push_back(d.at(U"code").string);
    return out;
}
bool LaunchRequest::process_launch_allowed() const noexcept { return impl_->request.at(U"process_launch_allowed").boolean; }
std::string LaunchRequest::export_json() const { return compat::display(impl_->request); }
LaunchRequest begin_launch(const Manifest& manifest, std::u32string_view association_id, const DiscoveryObservation& observation,
                           const LaunchSelection& selection, std::u32string_view id, std::u32string_view created_at) {
    return pi::begin_launch(manifest, association_id, observation, arguments_value(selection), id, created_at);
}
LaunchRequest evaluate_launch(const LaunchRequest& stage_one, const catalog::Projection& projection, const StateObservation& state_observation) {
    const auto& in = PlanningAccess::impl(stage_one);
    if (in.complete) throw std::invalid_argument("launch request is already complete");
    const Value& catalog = *catalog::projection_value(projection);
    const Value& state = PlanningAccess::value(state_observation);
    const Value &association = in.association, &instance = in.instance, &hunter = in.hunter, &observation = in.observation, &arguments = in.arguments;
    auto p = PlanningAccess::request_impl(stage_one);
    p->complete = true;
    Value& request = p->request;
    // Every stage-two key is appended before references into the object are
    // taken; affordability is filled in place once its inputs are known.
    // Broad parsing warnings remain visible even when not selection blockers.
    request.object.emplace_back(U"catalog_diagnostics", catalog.at(U"diagnostics"));
    auto source = pi::object_value();
    source.object = {{U"path", catalog.at(U"source")}, {U"sha256", catalog.at(U"source_sha256")}};
    request.object.emplace_back(U"catalog_source", std::move(source));
    request.object.emplace_back(U"state_observation", state);
    request.object.emplace_back(U"affordability", Value{});
    Value& capabilities = field(request, U"capabilities");
    Value& diagnostics = field(request, U"diagnostics");
    update(capabilities, catalog.at(U"capabilities"));
    std::vector<const Value*> saves;
    const Value* files = get(mapping(state, "state observation is not a mapping"), U"files");
    const Value listed = files ? as_list(*files) : pi::array_value();
    for (const auto& f : listed.array) if (schema::equal(mapping(f, "state file is not a mapping").at(U"kind"), pi::ascii_value("sav"))) saves.push_back(&f);
    Value empty = pi::object_value();
    const Value& decoded = saves.size() == 1 ? mapping(saves[0]->at(U"decoded"), "decoded save is not a mapping") : empty;
    const Value* roundtrip = get(decoded, U"codec_roundtrip_exact");
    const bool readable = roundtrip && schema::truth(*roundtrip);
    field(capabilities, U"native_save_format_readable") = pi::string_value(readable ? U"layout-candidate" : U"unknown");
    const Value* revision_changed = get(observation, U"revision_changed");
    if ((revision_changed && schema::truth(*revision_changed)) || !schema::equal(association.at(U"revision"), instance.at(U"revision")))
        diagnostics.array.push_back(diagnostic(U"revision-review-required", U"Content no longer matches association provenance."));
    if (!pi::is_text(state.at(U"status"), U"unchanged-state"))
        diagnostics.array.push_back(diagnostic(U"native-state-review-required", U"Authoritative state missing or changed since association."));
    if (const Value* archived = get(mapping(hunter, "association hunter is absent"), U"archived_at"); archived && schema::truth(*archived))
        diagnostics.array.push_back(diagnostic(U"archived-hunter", U"Archived hunter cannot prepare a hunt."));
    if (!pi::is_text(association.at(U"origin"), U"personal"))
        diagnostics.array.push_back(diagnostic(U"unclaimed-personal-progression", U"Packaged or unknown source is not certified personal progression."));
    if (!readable)
        diagnostics.array.push_back(diagnostic(U"save-format-unresolved", U"No supported codec observation for the associated save."));
    if (const Value* state_diagnostics = get(state, U"diagnostics"); state_diagnostics && [&] {
            for (const auto& d : as_list(*state_diagnostics).array) {
                const auto& code = mapping(d, "state diagnostic is not a mapping").at(U"code");
                for (auto c : {U"registration-mismatch", U"slot-outside-menu", U"noncanonical-slot-name", U"non-root-state", U"unreadable-layout", U"unclassified-companion"})
                    if (pi::is_text(code, c)) return true;
            }
            return false;
        }())
        diagnostics.array.push_back(diagnostic(U"native-state-ineligible", U"Native slot or state layout requires reconciliation."));
    const Value& effective = catalog.at(U"dialect").at(U"effective");
    if (!pi::is_text(effective, U"mee-newer") && !pi::is_text(effective, U"c2-classic"))
        diagnostics.array.push_back(diagnostic(U"dialect-launch-unresolved", U"This dialect has no candidate modern launch adapter."));
    if (has_code(catalog.at(U"diagnostics"), {U"unclosed-block", U"unmatched-brace", U"dialect-conflict"}))
        diagnostics.array.push_back(diagnostic(U"catalog-parse-review-required", U"Script ambiguity prevents validated selection."));
    const Value &mode = arguments.at(U"mode"), &time_of_day = arguments.at(U"time_of_day");
    const bool hunt = pi::is_text(mode, U"hunt"), observer = pi::is_text(mode, U"observer");
    if ((!hunt && !observer) || !time_of_day_ok(time_of_day))
        diagnostics.array.push_back(diagnostic(U"unsupported-mode", U"Only normal/observer planning and dawn/day/night values are modeled."));
    if (hunt && (!schema::truth(arguments.at(U"licenses")) || !schema::truth(arguments.at(U"weapons"))))
        diagnostics.array.push_back(diagnostic(U"empty-hunt-selection", U"Normal hunt requires at least one license and weapon."));
    const std::u32string groups[4] = {U"areas", U"licenses", U"weapons", U"equipment"};
    Value area_ids = pi::array_value(); area_ids.array.push_back(arguments.at(U"area"));
    const Value ids[4] = {area_ids, as_list(arguments.at(U"licenses")), as_list(arguments.at(U"weapons")), as_list(arguments.at(U"equipment"))};
    std::vector<const Value*> chosen[4];
    for (std::size_t g = 0; g < 4; ++g) {
        std::map<std::u32string, const Value*> known;
        for (const auto& entry : catalog.at(groups[g]).array) known[entry.at(U"id").string] = &entry;
        if (distinct(ids[g]) != ids[g].array.size())
            diagnostics.array.push_back(diagnostic(U"duplicate-selection", U"Selection repeats an entry ID.", U"category", pi::string_value(groups[g])));
        for (const auto& identity : ids[g].array) {
            if (!hashable(identity)) throw std::invalid_argument("unhashable selection identity");
            auto found = identity.kind == Kind::string ? known.find(identity.string) : known.end();
            if (found == known.end()) diagnostics.array.push_back(diagnostic(U"unknown-selection", U"Entry is not in this revision catalog.", U"entry_id", identity));
            else chosen[g].push_back(found->second);
        }
    }
    if (schema::truth(arguments.at(U"equipment")))
        diagnostics.array.push_back(diagnostic(U"equipment-policy-unresolved", U"Equipment meanings/flags require a dialect-specific policy."));
    for (std::size_t g = 1; g <= 2; ++g)
        if (std::any_of(chosen[g].begin(), chosen[g].end(), [](const Value* e) { return pi::compare_decimal(e->at(U"ordinal").integer, "10") >= 0; }))
            diagnostics.array.push_back(diagnostic(U"selection-mask-limit", U"Prototype candidate adapter is bounded to the evidenced first ten entries.", U"category", pi::string_value(groups[g])));
    for (const auto& d : catalog.at(U"diagnostics").array) {
        if (pi::is_text(d.at(U"code"), U"unusual-label") && contains_text(arguments.at(U"licenses"), d.at(U"entry_id"))) {
            diagnostics.array.push_back(diagnostic(U"license-meaning-unresolved", U"Instruction-like or blank entry requires edition-specific review."));
            break;
        }
    }
    const Value* area = chosen[0].empty() ? nullptr : chosen[0].front();
    if (!area || area->at(U"launch_stem").kind == Kind::null)
        diagnostics.array.push_back(diagnostic(U"area-unlaunchable", U"Advertised area has no unambiguous resource pair."));
    std::vector<const Value*> selected;
    for (const auto& group : chosen) selected.insert(selected.end(), group.begin(), group.end());
    const bool known_cost = std::all_of(selected.begin(), selected.end(), [](const Value* e) {
        const auto& price = e->at(U"price");
        return exact_int(price) && pi::compare_decimal(price.integer, "0") >= 0;
    });
    std::optional<std::string> cost;
    if (known_cost) {
        cost = "0";
        for (const Value* e : selected) cost = pi::add_decimal(*cost, e->at(U"price").integer);
    }
    const Value* score = get(decoded, U"score");
    if (score && score->kind == Kind::null) score = nullptr;
    std::optional<int> order;
    if (cost && score) order = pi::compare_number(*cost, *score);
    auto affordability = pi::object_value();
    affordability.object = {{U"native_score", score ? *score : Value{}}, {U"listed_selection_requirement", cost ? pi::integer_value(*cost) : Value{}},
        {U"meets_listed_requirement", cost && score ? pi::boolean_value(order && *order <= 0) : Value{}},
        {U"progression_mutation", pi::string_value(U"none")}, {U"rank_policy", pi::string_value(U"unresolved")},
        {U"score_modifiers", catalog.at(U"score_modifier_observations")}};
    field(request, U"affordability") = std::move(affordability);
    if (!cost) diagnostics.array.push_back(diagnostic(U"price-policy-unresolved", U"Missing or unusual price; no guessed defaults."));
    else if (score && order && *order > 0) diagnostics.array.push_back(diagnostic(U"insufficient-listed-score", U"Selection exceeds the observed native score."));
    if (diagnostics.array.empty()) {
        // Reached only with an exact filename slot, a resolved area, exact
        // dawn/day/night and every chosen ordinal below the ten-entry mask limit.
        const Value& slot = association.at(U"filename_slot");
        if (!exact_int(slot)) throw std::logic_error("validated filename_slot is not an exact integer");
        auto mask = [](const std::vector<const Value*>& entries) {
            std::uint32_t sum = 0;
            for (const Value* e : entries) {
                const auto& ordinal = e->at(U"ordinal").integer;
                // Canonical non-negative decimal below ten before any native shift:
                // the reference raises ValueError for 1 << negative, and std::stoul
                // would wrap a sign into an undefined shift count.
                if (!pi::canonical_decimal(ordinal) || ordinal[0] == '-') throw std::logic_error("ordinal is not a canonical non-negative integer");
                if (pi::compare_decimal(ordinal, "10") >= 0) throw std::logic_error("ordinal above the evidenced mask limit");
                sum += std::uint32_t{1} << std::stoul(ordinal);
            }
            return digits(sum);
        };
        field(request, U"selection_status") = pi::string_value(U"structurally-valid-policy-unverified");
        auto argv = pi::array_value();
        argv.array = {pi::string_value(U"reg=" + decimal_text(slot.integer)), pi::string_value(U"prj=huntdat/areas/" + area->at(U"launch_stem").string),
            pi::string_value(U"din=" + mask(chosen[1])), pi::string_value(U"wep=" + mask(chosen[2])), pi::string_value(U"dtm=" + decimal_text(time_of_day.integer))};
        if (observer) argv.array.push_back(pi::string_value(U"-observ"));
        field(request, U"candidate_argv") = std::move(argv);
    }
    // Always explicit, even when the candidate intent is structurally valid.
    diagnostics.array.push_back(diagnostic(U"engine-state-root-seam-required", U"Execution disabled: no isolated writable state-root and session ownership/return transaction adapter."));
    diagnostics.array.push_back(diagnostic(U"progression-policy-unverified", U"No rank/unlock/settlement equivalence has been certified."));
    return PlanningAccess::request(std::move(p));
}
}
namespace c2::frontend::planning_internal {
using compat::Kind;
using planning::Error;
planning::LaunchRequest begin_launch(const Manifest& manifest, std::u32string_view association_id, const DiscoveryObservation& observed,
                                     const Value& arguments, std::u32string_view id, std::u32string_view created_at) {
    const Value& data = ManifestAccess::data(manifest);
    const Value& associations = data.at(U"associations");
    if (!associations.contains(association_id)) throw Error("unknown association ID");
    const Value& association = associations.at(association_id);
    const Value& instances = data.at(U"instances");
    const Value& instance_id = association.at(U"instance_id");
    if (instance_id.kind != Kind::string || !instances.contains(instance_id.string)) throw Error("unknown instance ID");
    const Value& instance = instances.at(instance_id.string);
    const Value& observation = discovery_internal::observation_value(observed);
    auto p = planning::PlanningAccess::request_impl();
    p->association = association; p->instance = instance; p->observation = observation; p->arguments = arguments;
    // The reference indexes hunters only in stage two; retain whatever is there.
    const Value& hunters = data.at(U"hunters");
    const auto& hunter_id = association.at(U"hunter_id").string;
    p->hunter = hunters.contains(hunter_id) ? hunters.at(hunter_id) : Value{};
    const bool recognized = schema::truth(observation.at(U"recognized"));
    const Value* capabilities_observed = observation.contains(U"capabilities") ? &observation.at(U"capabilities") : nullptr;
    const Value* bundled = capabilities_observed && capabilities_observed->contains(U"bundled_engine_evidence") ? &capabilities_observed->at(U"bundled_engine_evidence") : nullptr;
    bool fallback = false;
    if (observation.contains(U"executables"))
        for (const auto& name : observation.at(U"executables").array) {
            auto lowered = schema::lower(name.string);
            if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, U".exe") == 0) { fallback = true; break; }
        }
    auto capabilities = object_value();
    capabilities.object = {{U"installation_recognized", string_value(recognized ? U"yes" : U"no")}, {U"content_recognized", string_value(recognized ? U"yes" : U"no")},
        {U"bundled_engine_evidence", bundled ? *bundled : string_value(U"unknown")}, {U"native_profile_associated", string_value(U"yes")},
        {U"native_save_format_readable", string_value(U"unknown")}, {U"content_dialect_recognized", string_value(U"unknown")},
        {U"console_can_be_generated", string_value(U"unknown")}, {U"modern_engine_compatibility", string_value(U"unknown")},
        {U"launch_tested", string_value(U"unknown")}, {U"hunt_save_round_trip_validated", string_value(U"unknown")},
        {U"trophy_interpretation_validated", string_value(U"unknown")},
        {U"legacy_windows_fallback_available", string_value(fallback ? U"candidate-files-only" : U"unknown")}};
    auto selection = object_value();
    selection.object = {{U"area", arguments.at(U"area")}, {U"licenses", planning::as_list(arguments.at(U"licenses"))},
        {U"weapons", planning::as_list(arguments.at(U"weapons"))}, {U"equipment", planning::as_list(arguments.at(U"equipment"))},
        {U"mode", arguments.at(U"mode")}, {U"time_of_day", arguments.at(U"time_of_day")}};
    auto& request = p->request;
    request = object_value();
    request.object = {{U"schema_version", integer_value("1")}, {U"id", string_value(std::u32string(id))}, {U"created_at", string_value(std::u32string(created_at))},
        {U"kind", string_value(U"hunt-launch-dry-run")}, {U"hunter_id", association.at(U"hunter_id")}, {U"instance_id", instance.at(U"id")},
        {U"revision", instance.at(U"revision")}, {U"association_id", string_value(std::u32string(association_id))},
        {U"state_authority", association.at(U"authority")}, {U"native_slot", association.at(U"filename_slot")},
        {U"selection", std::move(selection)}, {U"host_settings", data.at(U"host_settings")}, {U"capabilities", std::move(capabilities)},
        {U"diagnostics", observation.at(U"diagnostics")}, {U"process_launch_allowed", boolean_value(false)}, {U"executable", Value{}},
        {U"cwd", instance.at(U"path")}, {U"candidate_argv", array_value()}, {U"selection_status", string_value(U"blocked")},
        {U"result", string_value(U"dry-run-only")}};
    p->complete = !recognized;
    return planning::PlanningAccess::request(std::move(p));
}
}
