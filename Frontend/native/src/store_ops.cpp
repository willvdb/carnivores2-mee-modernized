// Workstream B: manifest mutations, registration and imports. See store_ops.hpp.
#include "store_ops.hpp"
#include "c2/frontend/core.hpp"
#include "manifest_schema.hpp"
#include "planning_internal.hpp"
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
        assign(data, U"active_hunter", string_value(id));
    } else throw StoreError("unknown hunter action");
    return record;
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
