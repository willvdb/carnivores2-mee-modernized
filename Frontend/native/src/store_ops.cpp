// Workstream B: manifest mutations, registration and imports. See store_ops.hpp.
#include "store_ops.hpp"
#include "c2/frontend/content.hpp"
#include "c2/frontend/core.hpp"
#include "c2/frontend/discovery.hpp"
#include "c2/frontend/profile_files.hpp"
#include "capture.hpp"
#include "content_internal.hpp"
#include "discovery_internal.hpp"
#include "manifest_access.hpp"
#include "manifest_schema.hpp"
#include "planning_internal.hpp"
#include "probe_process.hpp"
#include "schema_compat.hpp"
#include "session_journal.hpp"
#include "store_paths.hpp"
#include <algorithm>
#include <cerrno>
#include <system_error>
namespace fs = std::filesystem;
namespace c2::frontend::store_ops {
namespace {
using compat::Kind;
using planning_internal::ascii_value;
using planning_internal::object_value;
using planning_internal::string_value;
Value* member(Value& object, std::u32string_view key) {
    if (object.kind != Kind::object) return nullptr;
    for (auto& m : object.object) if (m.first == key) return &m.second;
    return nullptr;
}
// dict[key] = value: replace in place, otherwise append (insertion order).
void assign(Value& object, std::u32string_view key, Value value) {
    if (Value* existing = member(object, key)) { *existing = std::move(value); return; }
    object.object.emplace_back(std::u32string(key), std::move(value));
}
// A validated manifest always has its tables; the reference would KeyError.
Value& table(Value& data, std::u32string_view key) {
    Value* v = member(data, key);
    if (!v || v->kind != Kind::object) throw StoreError("malformed manifest table");
    return *v;
}
std::u32string ascii(std::string_view s) { return {s.begin(), s.end()}; }
std::string utf8(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(static_cast<char>(0xc0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
        else if (c < 0x10000) { out.push_back(static_cast<char>(0xe0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
        else { out.push_back(static_cast<char>(0xf0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 63))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
    }
    return out;
}
// repr(str(path)) for the reference's wrapped FileNotFoundError text.
std::string python_repr(const fs::path& path) {
    const std::string text = utf8(store_paths::native_points(path));
    const bool single = text.find('\'') != std::string::npos && text.find('"') == std::string::npos;
    const char quote = single ? '"' : '\'';
    std::string out(1, quote);
    for (char c : text) {
        if (c == '\\' || c == quote) out.push_back('\\');
        out.push_back(c);
    }
    out.push_back(quote);
    return out;
}
// store.read_manifest: decode failures carry the reference prefix (with a
// native detail text); validation refusals carry their own reference message.
Value read_manifest(const fs::path& path, const ReadPolicy& policy) {
    std::optional<std::string> bytes;
    try { bytes = store_paths::read(path, policy); }
    catch (const ResourceExhausted&) { throw; }
    catch (const StoreError& e) { throw StoreError(std::string("cannot read manifest: ") + e.what()); }
    if (!bytes) throw StoreError("cannot read manifest: [Errno 2] No such file or directory: " + python_repr(path));
    Value value;
    try { value = compat::parse(*bytes, policy.max_depth); }
    catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
    catch (const compat::Error& e) { throw StoreError(std::string("cannot read manifest: ") + e.what()); }
    try { schema::validate_manifest(value); }
    catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
    catch (const compat::Error& e) { throw StoreError(e.what()); }
    return value;
}
bool version_two(const Value& manifest) { return manifest.at(U"schema_version").integer == "2"; }
#ifdef _WIN32
constexpr bool native_nt = true;
#else
constexpr bool native_nt = false;
#endif
const std::u32string_view flavor = session_journal::path_flavor();
using planning_internal::array_value;
namespace ci = content_internal;
// Path(text): pathlib construction spelling for native locators.
fs::path native_units(const std::u32string& points) { return ci::source_spelling(ci::native_units(points)); }
Value optional_text(const std::optional<std::u32string>& s) { return s ? string_value(*s) : Value{}; }
// pathlib equality: exact parts on POSIX, lower() on NT (as PurePath::below).
bool same_path(const fs::path& a, const fs::path& b) {
    auto x = schema::path(store_paths::native_points(a), native_nt), y = schema::path(store_paths::native_points(b), native_nt);
    if (native_nt) {
        x.drive = schema::lower(x.drive); y.drive = schema::lower(y.drive);
        for (auto& s : x.parts) s = schema::lower(s);
        for (auto& s : y.parts) s = schema::lower(s);
    }
    return x.drive == y.drive && x.root == y.root && x.parts == y.parts;
}
bool strictly_below(const fs::path& child, const fs::path& parent) {
    return schema::path(store_paths::native_points(child), native_nt).below(schema::path(store_paths::native_points(parent), native_nt));
}
bool is_dir(const fs::path& p) { return fs::is_directory(ci::source_status(p)); }
Value locator_value(const fs::path& path) {
    Value v = object_value();
    v.object = {{U"path", string_value(store_paths::native_points(path))}, {U"path_flavor", string_value(std::u32string(flavor))}};
    return v;
}
// store.validate_locator over a retained value (relocate rechecks managed_root).
void validate_locator(const Value& v) {
    const bool object = v.kind == Kind::object;
    const Value* flavor_value = object && v.contains(U"path_flavor") ? &v.at(U"path_flavor") : nullptr;
    const bool nt = flavor_value && flavor_value->kind == Kind::string && flavor_value->string == U"nt";
    if (!nt && !(flavor_value && flavor_value->kind == Kind::string && flavor_value->string == U"posix"))
        throw StoreError("invalid path flavor or locator");
    const Value* path = v.contains(U"path") ? &v.at(U"path") : nullptr;
    if (!path || path->kind != Kind::string || path->string.empty() || path->string.find(U'\0') != std::u32string::npos)
        throw StoreError("invalid installation locator");
    const auto parsed = schema::path(path->string, nt);
    if (!parsed.absolute() || parsed.parent()) throw StoreError("locator must be absolute without parent traversal");
}
Value revision_value(const fs::path& root) {
    const auto f = fingerprint(root);
    Value v = object_value();
    v.object = {{U"algorithm", ascii_value(f.algorithm())}, {U"sha256", ascii_value(f.sha256())},
        {U"file_count", planning_internal::integer_value(std::to_string(f.file_count()))},
        {U"byte_count", planning_internal::integer_value(f.byte_count())}};
    return v;
}
Value engines_value(const fs::path& root, const DiscoveryObservation& observation) {
    Value out = array_value();
    for (const auto& e : engine_evidence(root, observation)) {
        Value item = object_value();
        item.object = {{U"path", string_value(e.path)}, {U"sha256", ascii_value(e.sha256)}, {U"semantics", string_value(e.semantics)}};
        out.array.push_back(std::move(item));
    }
    return out;
}
// dict.setdefault(key, []).append(...): retained unknown metadata of another
// kind is the reference AttributeError.
Value& list_default(Value& object, std::u32string_view key) {
    if (!object.contains(key)) object.object.emplace_back(std::u32string(key), array_value());
    Value& v = *member(object, key);
    if (v.kind != Kind::array) throw std::invalid_argument("retained instance list is not a list");
    return v;
}
bool registered_here(const Value& instance, const fs::path& root) {
    return instance.at(U"path_flavor").string == flavor &&
        same_path(store_paths::resolve_native(native_units(instance.at(U"path").string)), root);
}
std::string narrow_ascii(const std::u32string& s) {
    std::string out;
    for (const char32_t c : s) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    return out;
}
bool has_diagnostic(const Value& diagnostics, std::u32string_view code) {
    for (const auto& d : diagnostics.array)
        if (d.kind == Kind::object && d.contains(U"code") && planning_internal::is_text(d.at(U"code"), code)) return true;
    return false;
}
// managed_state.members_from_import: sorted [{path, size, sha256, type: file}].
Value members_from_import(const Value& association) {
    Value out = array_value();
    for (const auto& f : association.at(U"files").array) {
        Value m = object_value();
        m.object = {{U"path", f.at(U"path")}, {U"size", f.at(U"size")}, {U"sha256", f.at(U"sha256")}, {U"type", ascii_value("file")}};
        out.array.push_back(std::move(m));
    }
    std::stable_sort(out.array.begin(), out.array.end(), [](const Value& a, const Value& b) {
        return a.at(U"path").string < b.at(U"path").string;
    });
    return out;
}
Value provenance(const Value& association) {
    Value out = object_value();
    for (auto key : {U"id", U"hunter_id", U"instance_id", U"filename_slot", U"state_key", U"origin", U"revision"})
        out.object.emplace_back(key, association.at(key));
    return out;
}
// managed_state.initialize_history on a fresh import or an upgraded association.
void initialize_history(Value& association) {
    if (association.contains(U"managed_state"))
        throw StoreError("reserved managed_state metadata already exists; explicit review required");
    const std::u32string id = ascii(store_write::new_id());
    assign(association, U"authority", ascii_value("managed-state-history"));
    Value generation = object_value();
    generation.object = {{U"id", string_value(id)}, {U"sequence", planning_internal::integer_value("0")},
        {U"predecessor", Value{}}, {U"source_session", Value{}}, {U"kind", ascii_value("original-import")},
        {U"created_at", ascii_value(store_write::now())}, {U"members", members_from_import(association)},
        {U"provenance", provenance(association)}, {U"snapshot", string_value(U"snapshots/" + association.at(U"id").string)}};
    Value generations = object_value();
    generations.object.emplace_back(id, std::move(generation));
    Value history = object_value();
    history.object = {{U"schema_version", planning_internal::integer_value("1")}, {U"current_generation", string_value(id)},
        {U"generations", std::move(generations)}, {U"receipts", object_value()}};
    association.object.emplace_back(U"managed_state", std::move(history));
}
}

Value associate(const Store& store, Value& data, std::u32string_view hunter_id, std::u32string_view instance_id,
                std::u32string_view state_key, std::u32string_view origin, const std::optional<std::u32string>& ownership_option,
                const std::optional<fs::path>& probe, const store_write::FailureHook& hook) {
    const Value& hunters = table(data, U"hunters");
    if (!hunters.contains(hunter_id) || (hunters.at(hunter_id).contains(U"archived_at") && schema::truth(hunters.at(hunter_id).at(U"archived_at"))))
        throw StoreError("association requires an active hunter identity");
    if (origin != U"personal" && origin != U"bundled-example" && origin != U"unknown")
        throw StoreError("explicit source declaration required: personal, bundled-example, or unknown");
    const Value* instance = member(table(data, U"instances"), instance_id);
    if (!instance) throw StoreError("unknown instance ID");
    const Value observation = discovery_internal::inspect(*instance);
    if (!observation.at(U"recognized").boolean || (observation.contains(U"revision_changed") && schema::truth(observation.at(U"revision_changed"))))
        throw StoreError("installation unavailable or revision changed; refresh and review before association");
    const std::u32string ownership = ownership_option && !ownership_option->empty() ? *ownership_option
        : (instance->at(U"mode").string == U"managed" ? U"managed" : U"referenced");
    const bool managed = ownership == U"managed";
    if (!managed && ownership != U"referenced") throw StoreError("invalid ownership mode");
    Value& associations = table(data, U"associations");
    if (!managed)
        for (const auto& entry : associations.object) {
            const Value& a = entry.second;
            if (a.at(U"instance_id").string == instance_id && a.at(U"state_key").string == state_key && a.at(U"ownership").string == U"referenced")
                throw StoreError("source already referenced; choose an explicit independent managed copy");
        }
    const fs::path root = native_units(instance->at(U"path").string);
    const auto inventory = inventory_profiles(root);
    const ProfileState* state = nullptr;
    for (const auto& s : inventory.states()) if (s.key() == state_key) { state = &s; break; }
    bool has_save = false;
    if (state) for (const auto& f : state->files()) has_save = has_save || f.kind == U"sav";
    if (!state || !has_save) throw StoreError("selected state has no save; orphan rooms are never adopted as profiles");
    if (managed && !state->companions().empty())
        throw StoreError("unclassified companion files require review before managed import; reference-only inspection remains available");
    const Value inspection = probe_process::inspect_set(*state, probe, narrow_ascii(instance->at(U"dialect_hint").string));
    if (has_diagnostic(inspection.at(U"diagnostics"), U"registration-mismatch"))
        throw StoreError("registration mismatch requires explicit future reconciliation; source unchanged");
    const std::u32string id = ascii(store_write::new_id());
    Value files = array_value();
    for (const auto& f : inspection.at(U"files").array) {
        Value entry = object_value();
        for (const auto& field : f.object) if (field.first != U"decoded") entry.object.push_back(field);
        files.array.push_back(std::move(entry));
    }
    Value association = object_value();
    association.object = {{U"id", string_value(id)}, {U"hunter_id", string_value(std::u32string(hunter_id))},
        {U"instance_id", string_value(std::u32string(instance_id))}, {U"state_key", string_value(std::u32string(state_key))},
        {U"filename_slot", inspection.at(U"filename_slot")}, {U"origin", string_value(std::u32string(origin))},
        {U"ownership", string_value(ownership)}, {U"authority", ascii_value(managed ? "independent-snapshot" : "native-files")},
        {U"writable", planning_internal::boolean_value(false)}, {U"revision", instance->at(U"revision")},
        {U"created_at", ascii_value(store_write::now())}, {U"files", std::move(files)},
        {U"unclassified_companions", inspection.at(U"unclassified_companions")}, {U"diagnostics", inspection.at(U"diagnostics")}};
    if (managed) {
        // Independent lossless copy: the exact stable bytes, staged under a
        // pending name and published by one directory replacement.
        const auto blobs = state->stable_read();
        std::vector<CapturedBlob> captured;
        for (const auto& f : association.at(U"files").array) {
            const ProfileBlob* blob = nullptr;
            for (const auto& b : blobs) if (b.path == f.at(U"path").string) blob = &b;
            if (!blob) throw std::logic_error("stable read omitted an inventoried member");
            if (sha256(blob->bytes) != narrow_ascii(f.at(U"sha256").string)) throw StoreError("state changed after inspection");
        }
        for (const auto& b : blobs) captured.push_back({b.path, b.bytes});
        const fs::path snapshots = store.directory() / "snapshots";
        const fs::path staging = snapshots / (".pending-" + narrow_ascii(id)), final = snapshots / narrow_ascii(id);
        // staging.mkdir(parents=True, exist_ok=False)
        if (fs::exists(fs::symlink_status(staging)))
            throw fs::filesystem_error("snapshot staging exists", staging, std::make_error_code(std::errc::file_exists));
        store_write::write_blobs(staging, captured, hook);
        std::error_code ec;
        fs::rename(staging, final, ec);
        if (ec) throw fs::filesystem_error("cannot publish snapshot", staging, final, ec);
        store_write::sync_directory(snapshots);
        association.object.emplace_back(U"snapshot", string_value(U"snapshots/" + id));
    }
    if (version_two(data) && managed) initialize_history(association);
    associations.object.emplace_back(id, association);
    return association;
}

Value upgrade_store(const Store& store, const store_write::FailureHook& hook) {
    store_paths::safe_path(store.directory());
    store_paths::safe_path(store.directory() / "lodge.json");
    store_write::WriterLock lock(store.directory(), hook);
    const Manifest manifest = store.read();
    const ReadPolicy& policy = ManifestAccess::policy(manifest);
    Value data = ManifestAccess::data(manifest);
    Value result = object_value();
    if (version_two(data)) {
        result.object = {{U"result", ascii_value("already-upgraded")}, {U"schema_version", planning_internal::integer_value("2")}};
        lock.release();
        return result;
    }
    // Reserved names in old unknown metadata cannot be silently reinterpreted.
    if (data.contains(U"state_upgrade")) throw StoreError("reserved upgrade metadata already exists");
    Value& associations = table(data, U"associations");
    for (const auto& entry : associations.object) {
        const Value& a = entry.second;
        if (a.contains(U"managed_state")) throw StoreError("reserved managed_state metadata already exists");
        if (a.at(U"ownership").string == U"managed") {
            const fs::path snapshot = store.directory() / "snapshots" / narrow_ascii(a.at(U"id").string);
            store_paths::safe_path(snapshot);
            if (!schema::equal(capture(snapshot).entry_value(), members_from_import(a)))
                throw StoreError("import snapshot differs from provenance; upgrade blocked");
        }
    }
    const fs::path path = store.directory() / "lodge.json";
    std::string before;
    if (const auto current = store_paths::read(path, policy)) before = *current;
    else before = session_journal::encode(data);
    const fs::path backup = store.directory() / "lodge.schema-1.backup.json";
    store_paths::safe_path(backup);
    auto backup_matches = [&] {
        const auto bytes = store_paths::read(backup, policy);
        return bytes && *bytes == before && schema::equal(read_manifest(backup, policy), data);
    };
    if (fs::exists(backup)) {
        if (!backup_matches()) throw StoreError("prior upgrade backup differs; retain for explicit review");
    } else store_write::atomic_write(backup, before, hook);
    if (!backup_matches()) throw StoreError("upgrade backup did not validate");
    for (auto& entry : associations.object)
        if (entry.second.at(U"ownership").string == U"managed") initialize_history(entry.second);
    assign(data, U"schema_version", planning_internal::integer_value("2"));
    Value provenance = object_value();
    provenance.object = {{U"from_version", planning_internal::integer_value("1")}, {U"at", ascii_value(store_write::now())},
        {U"backup", ascii_value("lodge.schema-1.backup.json")}, {U"backup_sha256", ascii_value(sha256(before))}};
    // Appending a top-level member may reallocate: the table reference is stale.
    assign(data, U"state_upgrade", std::move(provenance));
    try { schema::validate_manifest(data); }
    catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
    catch (const compat::Error& e) { throw StoreError(e.what()); }
    // Sole authority commit: native snapshots are unchanged; old readers reject
    // v2. This is the sorted JournalEvidenceV1 encoding, unlike a transaction.
    store_write::atomic_write(path, session_journal::encode(data), hook);
    Value upgraded = object_value();
    for (const auto& entry : table(data, U"associations").object)
        if (entry.second.at(U"ownership").string == U"managed")
            upgraded.object.emplace_back(entry.first, entry.second.at(U"managed_state").at(U"current_generation"));
    result.object = {{U"result", ascii_value("upgraded")}, {U"schema_version", planning_internal::integer_value("2")},
        {U"backup", ascii_value("lodge.schema-1.backup.json")}, {U"associations", std::move(upgraded)}};
    lock.release();
    return result;
}

Value register_instance(Value& data, const fs::path& path, std::u32string_view mode, std::u32string_view dialect,
                        const std::optional<std::u32string>& family, const std::optional<std::u32string>& release,
                        const std::optional<fs::path>& managed_root) {
    const fs::path root = native_path(path);
    static constexpr std::u32string_view dialects[] = {U"unknown", U"c2-classic", U"iceage-triassic", U"mee-older", U"mee-newer"};
    if ((mode != U"registered" && mode != U"managed") || std::find(std::begin(dialects), std::end(dialects), dialect) == std::end(dialects))
        throw StoreError("invalid installation mode or dialect");
    Value managed;
    if (mode == U"managed") {
        if (!managed_root) throw StoreError("managed installation requires an explicit Expeditions directory");
        const fs::path directory = native_path(*managed_root);
        if (!is_dir(directory) || same_path(root, directory) || !strictly_below(root, directory))
            throw StoreError("managed installation must be below the existing explicit Expeditions directory");
        managed = locator_value(directory);
    }
    Value& instances = table(data, U"instances");
    for (const auto& entry : instances.object) if (registered_here(entry.second, root)) return entry.second;
    const auto observation = recognize(root);
    const Value& evidence = discovery_internal::observation_value(observation);
    if (!observation.recognized())
        throw StoreError("not a coherent installation: " + compat::dumps(evidence.at(U"diagnostics"), false));
    const Value revision = revision_value(root);
    Value engines = engines_value(root, observation);
    const std::u32string id = ascii(store_write::new_id());
    const bool asserted = (family && !family->empty()) || (release && !release->empty()) || dialect != U"unknown";
    Value revisions = array_value();
    revisions.array.push_back(revision);
    Value instance = object_value();
    instance.object = {{U"id", string_value(id)}, {U"path", string_value(store_paths::native_points(root))},
        {U"path_flavor", string_value(std::u32string(flavor))}, {U"mode", string_value(std::u32string(mode))},
        {U"managed_root", std::move(managed)}, {U"family", optional_text(family)}, {U"release", optional_text(release)},
        {U"dialect_hint", string_value(std::u32string(dialect))},
        {U"identity_evidence", ascii_value(asserted ? "user assertion" : "unresolved")},
        {U"created_at", ascii_value(store_write::now())}, {U"revision", revision}, {U"revisions", std::move(revisions)},
        {U"evidence", evidence}, {U"engine_evidence", std::move(engines)}};
    instances.object.emplace_back(id, instance);
    return instance;
}

Value relocate(Value& data, std::u32string_view identity, const fs::path& path) {
    Value& instances = table(data, U"instances");
    Value* instance = member(instances, identity);
    if (!instance) throw StoreError("unknown instance ID");
    const fs::path root = native_path(path);
    const bool native = instance->at(U"path_flavor").string == flavor;
    if (native) {
        const fs::path old = native_units(instance->at(U"path").string);
        if (fs::exists(ci::source_status(old)) || ci::source_is_link(old))
            throw StoreError("old installation still exists; this could be a clone, not a move");
    }
    for (const auto& entry : instances.object)
        if (registered_here(entry.second, root)) throw StoreError("destination already registered");
    std::u32string mode = instance->at(U"mode").string;
    if (mode == U"managed") {
        const Value* managed = instance->contains(U"managed_root") ? &instance->at(U"managed_root") : nullptr;
        if (!managed || managed->kind == Kind::null)
            throw StoreError("managed root context missing; explicit ownership reconciliation required");
        validate_locator(*managed);
        if (managed->at(U"path_flavor").string != flavor || !native)
            throw StoreError("foreign managed root requires explicit ownership reconciliation");
        const fs::path directory = native_units(managed->at(U"path").string), previous = native_units(instance->at(U"path").string);
        if (!is_dir(directory) || !same_path(store_paths::resolve_native(directory), directory) ||
            !same_path(store_paths::resolve_native(previous), previous) || same_path(previous, directory) ||
            !strictly_below(previous, directory) || same_path(root, directory))
            throw StoreError("managed root missing or ambiguous; explicit ownership reconciliation required");
        mode = strictly_below(root, directory) ? U"managed" : U"registered";
    }
    const auto observation = recognize(root);
    if (!observation.recognized() || !schema::equal(revision_value(root), instance->at(U"revision")))
        throw StoreError("relocation requires a coherent root with matching content revision");
    Value engines = engines_value(root, observation);
    Value previous = object_value();
    previous.object = {{U"path", instance->at(U"path")}, {U"path_flavor", instance->at(U"path_flavor")}};
    if (!schema::equal(engines, instance->at(U"engine_evidence"))) {
        // The registration baseline remains immutable. Even returning to its
        // bytes later cannot erase a pending review of an explicit relocation.
        Value review = object_value();
        review.object = {{U"status", ascii_value("required")}, {U"observed_at", ascii_value(store_write::now())},
            {U"from", previous}, {U"to", locator_value(root)}, {U"baseline_engine_evidence", instance->at(U"engine_evidence")},
            {U"destination_engine_evidence", std::move(engines)}};
        list_default(*instance, U"engine_relocation_reviews").array.push_back(std::move(review));
    }
    list_default(*instance, U"previous_locations").array.push_back(std::move(previous));
    // Keep the root locator as provenance even after relinquishing ownership.
    // A registered instance never regains management merely by its destination.
    assign(*instance, U"path", string_value(store_paths::native_points(root)));
    assign(*instance, U"path_flavor", string_value(std::u32string(flavor)));
    assign(*instance, U"mode", string_value(std::move(mode)));
    return *instance;
}

Value refresh_instance(Value& data, std::u32string_view identity) {
    Value* instance = member(table(data, U"instances"), identity);
    if (!instance) throw StoreError("unknown instance ID");
    Value result = discovery_internal::inspect(*instance);
    if (result.contains(U"revision_changed") && schema::truth(result.at(U"revision_changed"))) {
        const Value revision = result.at(U"revision");
        assign(*instance, U"revision", revision);
        Value& revisions = list_default(*instance, U"revisions");
        const bool known = std::any_of(revisions.array.begin(), revisions.array.end(),
            [&](const Value& v) { return schema::equal(v, revision); });
        if (!known) revisions.array.push_back(revision);
    }
    assign(*instance, U"last_observation", result);
    return result;
}

Value discover_view(const Manifest& manifest, const fs::path& directory) {
    Value out = array_value();
    for (const auto& observation : discover(directory)) {
        Value result = discovery_internal::observation_value(observation);
        if (observation.recognized()) {
            Value moves = array_value();
            for (auto& id : move_candidates(manifest, native_units(result.at(U"path").string))) moves.array.push_back(string_value(std::move(id)));
            result.object.emplace_back(U"possible_moves", std::move(moves));
        }
        out.array.push_back(std::move(result));
    }
    return out;
}

Value discover_register(Value& data, const fs::path& directory) {
    // The reference completes discovery before registering the first root.
    std::vector<fs::path> roots;
    for (const auto& observation : discover(directory))
        if (observation.recognized()) roots.push_back(native_units(*observation.path()));
    Value out = array_value();
    for (const auto& root : roots)
        out.array.push_back(register_instance(data, root, U"managed", U"unknown", std::nullopt, std::nullopt, directory));
    return out;
}

Value hunter(Value& data, std::u32string_view action, const std::optional<std::u32string>& identity,
             const std::optional<std::u32string>& name) {
    const bool create = action == U"create", rename = action == U"rename";
    if ((create || rename) && (!name || schema::blank(*name)))
        throw StoreError("a nonblank hunter display name is required");
    Value& hunters = table(data, U"hunters");
    std::u32string id;
    if (create) {
        id = ascii(store_write::new_id());
        Value record = object_value();
        record.object = {{U"id", string_value(id)}, {U"name", string_value(*name)}, {U"created_at", ascii_value(store_write::now())}};
        hunters.object.emplace_back(id, std::move(record));
    } else if (!identity || !hunters.contains(*identity)) throw StoreError("unknown hunter ID");
    else id = *identity;
    Value& record = *member(hunters, id);
    if (rename) assign(record, U"name", string_value(*name));
    else if (action == U"archive") {
        assign(record, U"archived_at", ascii_value(store_write::now()));
        // data["active_hunter"] is indexed, not .get(): an absent optional
        // field is the reference KeyError (as the hunters view reports it).
        Value* active = member(data, U"active_hunter");
        if (!active) throw StoreError("active_hunter is absent");
        if (active->kind == Kind::string && active->string == id) *active = Value{};
    } else if (action == U"select" || create) {
        if (record.contains(U"archived_at") && schema::truth(record.at(U"archived_at")))
            throw StoreError("archived hunters cannot be selected");
        // Appending a top-level member may reallocate: look the record up again.
        assign(data, U"active_hunter", string_value(id));
    } else throw StoreError("unknown hunter action");
    return *member(table(data, U"hunters"), id);
}

Value update_host_settings(Value& data, std::string_view json_text) {
    // json.loads: a decode failure is the CLI's reported ValueError. Duplicate
    // keys are refused here rather than silently last-wins (documented).
    Value value;
    try { value = compat::parse(json_text); }
    catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
    catch (const compat::Error& e) { throw StoreError(e.what()); }
    bool valid = value.kind == Kind::object;
    for (const auto& m : value.object)
        valid = valid && (m.first == U"display" || m.first == U"audio" || m.first == U"input") && m.second.kind == Kind::object;
    if (!valid) throw StoreError("settings must be objects keyed by display, audio and/or input");
    Value& settings = table(data, U"host_settings");
    for (auto& m : value.object) assign(settings, m.first, std::move(m.second));
    return settings;
}

void restore_backup(const Store& store, const store_write::FailureHook& hook) {
    store_write::WriterLock lock(store.directory(), hook);
    const ReadPolicy policy;
    const fs::path path = store.directory() / "lodge.json", backup = store.directory() / "lodge.json.bak";
    // Existing schema-1 damaged-manifest recovery stays explicit: an unreadable
    // or invalid current manifest has no version.
    bool current_upgraded = false;
    if (const auto current = store_paths::read(path, policy)) {
        try {
            Value value = compat::parse(*current, policy.max_depth);
            schema::validate_manifest(value);
            current_upgraded = version_two(value);
        } catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
          catch (const compat::Error&) {}
    }
    if (current_upgraded || fs::exists(store.directory() / "lodge.schema-1.backup.json") ||
        version_two(read_manifest(backup, policy)))
        throw StoreError("managed-state backup restore requires explicit future recovery; no history rollback");
    if (const auto current = store_paths::read(path, policy))
        store_write::atomic_write(store.directory() / ("lodge.recovery-" + store_write::new_id() + ".json"), *current, hook);
    const auto restored = store_paths::read(backup, policy);
    if (!restored) throw fs::filesystem_error("cannot read backup", backup, std::make_error_code(std::errc::no_such_file_or_directory));
    store_write::atomic_write(path, *restored, hook);
    lock.release();
}
}
