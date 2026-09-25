#include "planning_store.hpp"
#include "c2/frontend/catalog.hpp"
#include "c2/frontend/discovery.hpp"
#include "c2/frontend/profile_files.hpp"
#include "content_internal.hpp"
#include "planning_internal.hpp"
#include "probe_process.hpp"
#include "session_journal.hpp"
#include "manifest_access.hpp"
#include "store_paths.hpp"
#include "capture.hpp"
#include "schema_compat.hpp"
#include "discovery_internal.hpp"
#include "catalog_internal.hpp"
#include <algorithm>
#include <map>
namespace fs = std::filesystem;
namespace c2::frontend::planning_store {
namespace {
using compat::Kind;
using compat::Value;
Value* member(Value& object, std::u32string_view key) {
    if (object.kind != Kind::object) return nullptr;
    for (auto& m : object.object) if (m.first == key) return &m.second;
    return nullptr;
}
const std::u32string& text(const Value& object, std::u32string_view key) {
    const Value& v = object.at(key);
    if (v.kind != Kind::string) throw std::invalid_argument("manifest field is not a string");
    return v.string;
}
std::map<std::u32string, std::u32string> hashes(const Value& files) {
    std::map<std::u32string, std::u32string> out;
    for (const Value& f : files.array) out[text(f, U"path")] = text(f, U"sha256");
    return out;
}
std::string narrow_ascii(const std::u32string& s) {
    std::string out;
    for (const char32_t c : s) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    return out;
}
}

namespace {
PinSnapshot snapshot_from(const Store& store, const Manifest& manifest, std::u32string_view association_id,
    const Value& selection, const std::optional<fs::path>& probe,
    const std::optional<Value>& expected_codec, std::u32string_view mode, bool managed,
    const std::optional<std::u32string>& generation_id) {
    using namespace planning_internal;
    const auto& data = ManifestAccess::data(manifest);
    const auto& associations = data.at(U"associations");
    if (!associations.contains(association_id)) throw StoreError("unknown association ID");
    const auto& association = associations.at(association_id);
    if (!is_text(association.at(U"ownership"), U"managed") || !is_text(association.at(U"origin"), U"personal"))
        throw StoreError("sessions require managed personal state; referenced/bundled/unknown rejected");
    const auto& hunter = data.at(U"hunters").at(text(association, U"hunter_id"));
    if (hunter.contains(U"archived_at") && schema::truth(hunter.at(U"archived_at")))
        throw StoreError("archived hunter cannot start a session");
    const auto observed_instance = get_instance(manifest, text(association, U"instance_id"));
    const auto& instance = discovery_internal::instance_value(observed_instance);
    const auto observation = inspect_instance(observed_instance);
    const auto& observed = discovery_internal::observation_value(observation);
    if (!observation.recognized() || observation.revision_changed().value_or(false) ||
        !schema::equal(association.at(U"revision"), instance.at(U"revision")))
        throw StoreError("session requires exact unchanged content revision");
    if (observation.engine_review_required().value_or(false))
        throw StoreError("engine evidence review blocks session");
    const auto& dialect = text(instance, U"dialect_hint");
    if (dialect != U"mee-newer" && dialect != U"c2-classic")
        throw StoreError("synthetic selection requires an explicit supported catalog hint");
    const auto projection = catalog::project(content_internal::native_units(text(instance, U"path")), dialect);
    const auto& catalog_value = *catalog::projection_value(projection);
    for (const auto& d : catalog_value.at(U"diagnostics").array) {
        const auto& code = text(d, U"code");
        if (code == U"unclosed-block" || code == U"unmatched-brace" || code == U"dialect-conflict" || code == U"explicit-areas-uninterpreted")
            throw StoreError("ambiguous catalog cannot prepare a session");
    }
    const std::u32string keys[] = {U"area", U"mode", U"time_of_day", U"licenses", U"weapons", U"equipment"};
    bool valid = selection.kind == Kind::object && selection.object.size() == 6;
    for (const auto& key : keys) valid = valid && selection.contains(key);
    if (valid) {
        const auto& time = selection.at(U"time_of_day");
        valid = is_text(selection.at(U"mode"), mode) && time.kind == Kind::integer &&
            (time.integer == "0" || time.integer == "1" || time.integer == "2") &&
            (mode != U"observer" || (!schema::truth(selection.at(U"licenses")) &&
             !schema::truth(selection.at(U"weapons")) && !schema::truth(selection.at(U"equipment"))));
    }
    if (!valid) throw StoreError("session policy permits observer with no loadout only");
    const Value* area = nullptr;
    for (const auto& item : catalog_value.at(U"areas").array)
        if (schema::equal(item.at(U"id"), selection.at(U"area"))) { area = &item; break; }
    if (!area || !schema::truth(area->at(U"launch_stem")))
        throw StoreError("session requires an unambiguous advertised area");
    const auto& slot_value = association.at(U"filename_slot");
    // Validated manifests have exact non-negative integer slots; compare the
    // canonical decimal before narrowing, including arbitrary-size outliers.
    if (slot_value.integer.size() != 1 || slot_value.integer[0] < '0' || slot_value.integer[0] > '7')
        throw StoreError("session requires a canonical root native slot");
    const int slot = slot_value.integer[0] - '0';
    const std::u32string stem = U"trophy0" + std::u32string(1, static_cast<char32_t>(U'0' + slot));
    if (text(association, U"state_key") != stem)
        throw StoreError("session requires a canonical root native slot");
    std::optional<GenerationObservation> generation;
    fs::path root;
    Capture captured;
    Value expected;
    if (managed) {
        generation.emplace(manifest.resolve_generation(std::u32string(association_id), generation_id));
        root = generation->root();
        captured.entries = generation->entries(); captured.blobs = generation->blobs();
        expected = ManifestAccess::generation(*generation).at(U"members");
    } else {
        if (manifest.schema_version() != 1)
            throw StoreError("legacy sessions cannot select an import in an upgraded store; prepare a generation-pinned hunt");
        root = store.directory() / "snapshots" / content_internal::native_units(text(association, U"id"));
        store_paths::safe_path(root);
        captured = capture(root);
        expected = array_value();
        for (const auto& file : association.at(U"files").array) {
            auto entry = object_value();
            for (const auto& key : {U"path", U"size", U"sha256"}) entry.object.emplace_back(key, file.at(key));
            entry.object.emplace_back(U"type", ascii_value("file"));
            expected.array.push_back(std::move(entry));
        }
        std::sort(expected.array.begin(), expected.array.end(), [](const Value& a, const Value& b) {
            return text(a, U"path") < text(b, U"path");
        });
    }
    const Value entries = captured.entry_value();
    bool has_save = false, allowed = true;
    for (const auto& blob : captured.blobs) {
        has_save = has_save || blob.path == stem + U".sav";
        allowed = allowed && (blob.path == stem + U".sav" || blob.path == stem + U".sab");
    }
    if (!schema::equal(entries, expected) || !has_save || !allowed)
        throw StoreError("managed source membership/bytes differ or require review");
    const Value codec = probe_process::codec_evidence(probe);
    if (expected_codec && !schema::equal(codec, *expected_codec))
        throw StoreError("pinned codec evidence changed; helper not executed");
    // Execute ONLY the resolved path whose evidence was just compared.
    const Value inspection = probe_process::inspect_bytes(captured.blobs, slot,
        content_internal::native_units(text(codec, U"path")));
    if (schema::truth(inspection.at(U"diagnostics")))
        throw StoreError("managed source is unreadable or has a registration mismatch");
    auto provenance = [](const Value& source, bool omit_history) {
        Value result = object_value();
        for (const auto& item : source.object)
            if (item.first != U"last_observation" && (!omit_history || item.first != U"managed_state"))
                result.object.push_back(item);
        return result;
    };
    Value pins = object_value();
    pins.object = {{U"hunter_id", hunter.at(U"id")}, {U"instance_id", instance.at(U"id")},
        {U"association_id", string_value(std::u32string(association_id))}, {U"hunter", hunter},
        {U"instance", provenance(instance, false)}, {U"association", provenance(association, generation.has_value())},
        {U"revision", instance.at(U"revision")}, {U"engine_evidence", observed.at(U"engine_evidence")},
        {U"native_slot", slot_value}, {U"source_root", string_value(store_paths::native_points(root))},
        {U"source_members", entries}, {U"source_observation", inspection.at(U"decoded")},
        {U"selection", selection}, {U"area_stem", area->at(U"launch_stem")}, {U"codec", codec},
        {U"adapter", ascii_value("frontend-synthetic-observer-v1")}};
    if (generation) {
        pins.object.emplace_back(U"generation_id", string_value(generation->id()));
        pins.object.emplace_back(U"generation", ManifestAccess::generation(*generation));
    }
    return {std::move(pins), std::move(captured.blobs)};
}
}
PinSnapshot snapshot_pins(const Store& store, std::u32string_view association_id,
    const Value& selection, const std::optional<fs::path>& probe,
    const std::optional<Value>& expected_codec, std::u32string_view mode, bool managed,
    const std::optional<std::u32string>& generation) {
    return snapshot_from(store, store.read(), association_id, selection, probe, expected_codec, mode, managed, generation);
}

namespace {
Value evaluate_pins(const Value& pins, const PolicyEvaluator& policy) {
    const auto& instance = pins.at(U"instance");
    const auto projection = catalog::project(content_internal::native_units(text(instance, U"path")),
        text(instance, U"dialect_hint"));
    const auto& slot = pins.at(U"native_slot");
    const std::u32string save = U"trophy0" + std::u32string(1, static_cast<char32_t>(slot.integer[0])) + U".sav";
    const auto& decoded = pins.at(U"source_observation").at(save);
    // A missing score is an unexpected reference KeyError, not a fabricated
    // zero or a readable-state diagnostic. The retained value lookup throws.
    return policy(pins.at(U"revision"), projection, slot, pins.at(U"selection"), decoded.at(U"score"));
}
}
Value plan_observer(const Store& store, std::u32string area, std::u32string_view association_id,
    const planning::Integer& time_of_day, const std::optional<fs::path>& probe,
    const std::optional<fs::path>& engine, const PolicyEvaluator& test_policy) {
    using namespace planning_internal;
    const auto selection = planning::Selection::observer(std::move(area), time_of_day);
    store_write::WriterLock lock(store.directory());
    auto captured = snapshot_from(store, store.read(), association_id, planning::PlanningAccess::value(selection),
        probe, std::nullopt, U"observer", false, std::nullopt);
    const auto policy = evaluate_pins(captured.pins, test_policy ? test_policy : PolicyEvaluator(observer_policy));
    *member(captured.pins, U"adapter") = string_value(std::u32string(planning::OBSERVER_POLICY_ID));
    Value result = object_value();
    result.object = {{U"kind", ascii_value("genesis-observer-blocked-plan")},
        {U"id", ascii_value(store_write::new_id())}, {U"created_at", ascii_value(store_write::now())},
        {U"pins", std::move(captured.pins)}};
    for (const auto& field : policy.object) result.object.push_back(field);
    result.object.emplace_back(U"selected_engine", engine && !engine->empty() ? probe_process::executable_evidence(*engine) : Value{});
    result.object.emplace_back(U"engine_evidence_status", ascii_value("hash-only-not-build-certification"));
    result.object.emplace_back(U"cwd", Value{});
    result.object.emplace_back(U"executable", Value{});
    lock.release();
    return result;
}
Value plan_hunt(const Store& store, std::u32string_view association_id, const Value& selection,
    const std::optional<fs::path>& probe, const PolicyEvaluator& test_policy) {
    using namespace planning_internal;
    // Python plan_hunt does not lock or persist. Select the schema adapter and
    // resolve pins from one immutable read, avoiding a schema/head read race.
    const auto manifest = store.read();
    auto captured = snapshot_from(store, manifest, association_id, selection, probe, std::nullopt,
        U"hunt", manifest.schema_version() == 2, std::nullopt);
    // snapshot_pins already requires a canonical SAV and excludes other names.
    if (captured.blobs.size() != 2)
        throw StoreError("hunt contract requires an existing complete SAV/SAB pair");
    const auto policy = evaluate_pins(captured.pins, test_policy ? test_policy : PolicyEvaluator(hunt_policy));
    *member(captured.pins, U"adapter") = string_value(std::u32string(planning::HUNT_POLICY_ID));
    captured.pins.object.emplace_back(U"hunt_policy", policy);
    Value result = object_value();
    result.object = {{U"kind", ascii_value("genesis-hunt-plan-v1")}, {U"pins", std::move(captured.pins)},
        {U"policy", policy}, {U"process_launch_allowed", boolean_value(false)}, {U"result", ascii_value("validated-intent-only")}};
    return result;
}

Value refresh_association(const Store& store, Value& data, std::u32string_view identity,
                          const std::optional<fs::path>& probe) {
    using namespace planning_internal;
    Value* associations = member(data, U"associations");
    Value* association = associations ? member(*associations, identity) : nullptr;
    if (!association) throw StoreError("unknown association ID");
    const Value* instances = member(data, U"instances");
    const std::u32string& instance_id = text(*association, U"instance_id");
    if (!instances || !instances->contains(instance_id)) throw StoreError("unknown instance ID");
    const Value& instance = instances->at(instance_id);
    // association_root
    fs::path root;
    std::optional<GenerationObservation> generation;
    if (text(*association, U"ownership") == U"referenced") {
        if (text(instance, U"path_flavor") != session_journal::path_flavor())
            throw StoreError("foreign installation path requires relocation");
        root = content_internal::native_units(text(instance, U"path"));
    } else if (association->contains(U"authority") && is_text(association->at(U"authority"), U"managed-state-history")) {
        generation.emplace(ManifestAccess::snapshot(store, data).resolve_generation(std::u32string(identity)));
        root = generation->root();
    } else {
        // Computed from the validated UUID, never from an arbitrary manifest path.
        const std::u32string& id = text(*association, U"id");
        root = store.directory() / "snapshots" / fs::path(std::string(id.begin(), id.end()));
    }
    const std::u32string& key = text(*association, U"state_key");
    const auto inventory = inventory_profiles(root);
    const ProfileState* state = nullptr;
    for (const auto& s : inventory.states()) if (s.key() == key) { state = &s; break; }
    Value result;
    if (!state) {
        result = object_value();
        result.object.emplace_back(U"status", ascii_value("missing-state"));
        Value diagnostics = array_value();
        diagnostics.array.push_back(diagnostic_value(U"missing-state", U"Native source remains associated; nothing deleted."));
        result.object.emplace_back(U"diagnostics", std::move(diagnostics));
    } else {
        result = probe_process::inspect_set(*state, probe, narrow_ascii(text(instance, U"dialect_hint")));
        // Match refresh_association's second validation before recording the
        // observation, against the SAME manifest head, never an older import.
        Value expected = association->at(U"files");
        if (generation) {
            const auto current = ManifestAccess::snapshot(store, data).resolve_generation(std::u32string(identity));
            expected = ManifestAccess::generation(current).at(U"members");
        }
        const bool changed = hashes(expected) != hashes(result.at(U"files"));
        result.object.emplace_back(U"status", ascii_value(changed ? "changed-state" : "unchanged-state"));
        if (changed)
            member(result, U"diagnostics")->array.push_back(diagnostic_value(U"state-drift",
                U"Native state differs from association baseline; no overwrite or normalization performed."));
    }
    if (Value* existing = member(*association, U"last_observation")) *existing = result;
    else association->object.emplace_back(U"last_observation", result);
    return result;
}

Value refresh_state(const Store& store, std::u32string_view identity, const std::optional<fs::path>& probe,
                    const store_write::FailureHook& hook) {
    Value result;
    store_write::transaction(store, [&](Value& data) { result = refresh_association(store, data, identity, probe); }, hook);
    return result;
}

planning::LaunchRequest launch_dry_run(const Store& store, std::u32string_view association_id,
                                       const planning::LaunchSelection& selection,
                                       const std::optional<fs::path>& probe, const store_write::FailureHook& hook) {
    std::optional<planning::LaunchRequest> request;
    store_write::transaction(store, [&](Value& data) {
        Value* associations = member(data, U"associations");
        Value* association = associations ? member(*associations, association_id) : nullptr;
        if (!association) throw StoreError("unknown association ID");
        const std::u32string instance_id = text(*association, U"instance_id");
        // Use exactly the transaction snapshot. The lock excludes cooperating
        // writers but cannot make an extra disk read the same immutable view.
        const Manifest manifest = ManifestAccess::snapshot(store, data);
        const InstanceObservation instance = get_instance(manifest, instance_id);
        const DiscoveryObservation observation = inspect_instance(instance);
        const std::string id = store_write::new_id(), created_at = store_write::now();
        auto stage_one = planning::begin_launch(manifest, association_id, observation, selection,
            std::u32string(id.begin(), id.end()), std::u32string(created_at.begin(), created_at.end()));
        if (stage_one.complete()) { request.emplace(std::move(stage_one)); return; }
        const Value& instance_value = member(data, U"instances")->at(instance_id);
        const auto projection = catalog::project(content_internal::native_units(text(instance_value, U"path")),
                                                 text(instance_value, U"dialect_hint"));
        const Value state = refresh_association(store, data, association_id, probe);
        request.emplace(planning::evaluate_launch(stage_one, projection, planning::PlanningAccess::state(state)));
    }, hook);
    return *request;
}
}
