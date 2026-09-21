#include "planning_store.hpp"
#include "c2/frontend/profile_files.hpp"
#include "content_internal.hpp"
#include "planning_internal.hpp"
#include "probe_process.hpp"
#include "session_journal.hpp"
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
    if (text(*association, U"ownership") == U"referenced") {
        if (text(instance, U"path_flavor") != session_journal::path_flavor())
            throw StoreError("foreign installation path requires relocation");
        root = content_internal::native_units(text(instance, U"path"));
    } else if (association->contains(U"authority") && is_text(association->at(U"authority"), U"managed-state-history")) {
        throw NotImplemented("refresh of a managed-state-history association is not implemented natively yet");
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
        const bool changed = hashes(association->at(U"files")) != hashes(result.at(U"files"));
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
}
