// Workstream A: lodge.reconciliation. Durable return candidates; never
// promotes or mutates association authority. See session_runner.hpp.
#include "session_runner.hpp"
#include "c2/frontend/catalog.hpp"
#include "c2/frontend/content.hpp"
#include "capture.hpp"
#include "content_internal.hpp"
#include "planning_internal.hpp"
#include "probe_process.hpp"
#include "schema_compat.hpp"
#include "session_journal.hpp"
#include "store_paths.hpp"
#include "store_write.hpp"
#include <algorithm>
#include <map>
#include <set>
namespace fs = std::filesystem;
namespace c2::frontend::reconciliation {
namespace {
using compat::Kind;
using compat::Value;
using namespace planning_internal;
const std::u32string& text(const Value& object, std::u32string_view key) {
    const Value& v = object.at(key);
    if (v.kind != Kind::string) throw std::invalid_argument("retained field is not a string");
    return v.string;
}
// dict.get(key): null when absent.
Value get(const Value& object, std::u32string_view key) { return object.contains(key) ? object.at(key) : Value{}; }
Value* member(Value& object, std::u32string_view key) {
    if (object.kind != Kind::object) return nullptr;
    for (auto& m : object.object) if (m.first == key) return &m.second;
    return nullptr;
}
void assign(Value& object, std::u32string_view key, Value value) {
    if (Value* existing = member(object, key)) { *existing = std::move(value); return; }
    object.object.emplace_back(std::u32string(key), std::move(value));
}
fs::path path_of(const Value& value) {
    if (value.kind != Kind::string) throw std::invalid_argument("retained path is not a string");
    return content_internal::native_units(value.string);
}
Value code_only(std::string_view code) {
    Value v = object_value();
    v.object.emplace_back(U"code", ascii_value(code));
    return v;
}
Value with_message(std::string_view code, const std::string& message) {
    Value v = code_only(code);
    v.object.emplace_back(U"message", string_value(store_paths::native_points(fs::u8path(message))));
    return v;
}
int schema_of(const Value& journal) {
    const Value& v = journal.at(U"schema_version");
    return v.kind == Kind::integer && v.integer.size() == 1 ? v.integer[0] - '0' : 0;
}
bool is_false(const Value& v) { return v.kind == Kind::boolean && !v.boolean; }
std::vector<std::u32string> entry_names(const fs::path& directory) {
    std::vector<std::u32string> names;
    for (const auto& entry : fs::directory_iterator(directory)) names.push_back(store_paths::native_points(entry.path().filename()));
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}
// The reference's `except (FrontendError, OSError, KeyError, TypeError, ValueError)`.
// FrontendError-derived messages are exact; OS, key and type messages are the
// native texts. Resource exhaustion and unexpected native failures propagate.
template <class F> std::optional<std::string> guarded(F&& f) {
    try { f(); return std::nullopt; }
    catch (const ResourceExhausted&) { throw; }
    catch (const StoreError& e) { return e.what(); }
    catch (const probe_process::ProbeError& e) { return e.what(); }
    catch (const planning::Error& e) { return e.what(); }
    catch (const catalog::Error& e) { return e.what(); }
    catch (const ContentError& e) { return e.what(); }
    catch (const fs::filesystem_error& e) { return e.what(); }
    catch (const std::invalid_argument& e) { return e.what(); }
    catch (const compat::Error& e) { return e.what(); }
}
bool file_or_directory(const std::vector<CapturedEntry>& entries) {
    return std::all_of(entries.begin(), entries.end(), [](const CapturedEntry& e) { return e.type == "file" || e.type == "directory"; });
}
std::map<std::u32string, const std::string*> blob_map(const std::vector<CapturedBlob>& blobs) {
    std::map<std::u32string, const std::string*> out;
    for (const auto& blob : blobs) out[blob.path] = &blob.bytes;
    return out;
}
bool same_blobs(const std::vector<CapturedBlob>& a, const std::vector<CapturedBlob>& b) {
    const auto x = blob_map(a), y = blob_map(b);
    if (x.size() != y.size()) return false;
    for (const auto& [path, bytes] : x) {
        const auto found = y.find(path);
        if (found == y.end() || *found->second != *bytes) return false;
    }
    return true;
}
} // namespace

Value reconcile_locked(const Store& store, const fs::path& root, Value& journal,
                       const std::optional<fs::path>& probe, const session_policy::Policies& policies) {
    if (is_text(journal.at(U"state"), U"returned"))
        session_journal::transition(root, journal, "inspecting", {{U"inspecting_at", ascii_value(store_write::now())}});
    if (!is_text(journal.at(U"state"), U"inspecting")) return journal;
    Value diagnostics = journal.at(U"diagnostics");
    const Value pins = journal.at(U"pins");
    const int schema = schema_of(journal);
    Value process = get(journal, U"process");
    if (!schema::truth(process)) process = object_value();
    else if (process.kind != Kind::object) throw std::invalid_argument("journal process is not an object");
    const Value exit_code = get(process, U"exit_code");
    if (exit_code.kind != Kind::integer || !is_text(get(process, U"stop_reason"), U"exited") || exit_code.integer != "0")
        diagnostics.array.push_back(code_only("unclean-process-return"));
    // None means unavailable, not an observed empty directory or corrupt pair.
    std::optional<Value> entries, decoded;
    Value observation = object_value();
    for (const auto key : {U"inventory", U"byte_capture", U"retained_capture", U"codec_inspection"})
        observation.object.emplace_back(key, ascii_value("unavailable"));
    if (const auto failure = guarded([&] {
        Value current;
        if (schema >= 2) {
            const auto adapter = native_session::adapter_for(journal);
            if (schema == 4) current = native_session::return_pins(store, pins, probe, policies).pins;
            else current = native_session::native_pins(adapter, store, text(pins, U"association_id"), pins.at(U"selection"),
                                                       probe, pins.at(U"codec"), policies).pins;
            const Value spec = journal.at(U"execution");
            const Value evidence = probe_process::executable_evidence(path_of(spec.at(U"executable").at(U"path")));
            if (!schema::equal(evidence, spec.at(U"executable"))) diagnostics.array.push_back(code_only("selected-engine-changed-on-return"));
            // Recovery validates evidence without executing any engine query.
            const Value expected = native_session::execution_spec(adapter, root, pins, evidence, native_session::capability(),
                                                                  get(spec, U"timeout_seconds"));
            if (!schema::equal(expected, spec) || !native_session::supported_contract(get(spec, U"contract")) || !is_false(get(spec, U"shell")))
                diagnostics.array.push_back(code_only("native-execution-evidence-changed-on-return"));
        } else {
            current = planning_store::snapshot_pins(store, text(pins, U"association_id"), pins.at(U"selection"), probe, pins.at(U"codec")).pins;
        }
        if (!schema::equal(current, pins)) diagnostics.array.push_back(code_only("pinned-evidence-changed-on-return"));
        const Value baseline = capture(root / "baseline").entry_value();
        if (!schema::equal(baseline, pins.at(U"source_members")) || !schema::equal(baseline, journal.at(U"baseline_members")))
            diagnostics.array.push_back(code_only("baseline-changed"));
    })) diagnostics.array.push_back(with_message("source-review-required", *failure));
    if (const auto failure = guarded([&] {
        store_paths::safe_path(root / "work");
        if (schema >= 2) {
            Value findings = native_session::workspace_findings(root, true);
            for (auto& finding : findings.array) diagnostics.array.push_back(std::move(finding));
        } else if (entry_names(root / "work") != std::vector<std::u32string>{U"state"}) {
            diagnostics.array.push_back(with_message("unexpected-workspace-entry", "All extra workspace entries retained in work."));
        }
    })) diagnostics.array.push_back(with_message("workspace-review-required", *failure));
    if (const auto failure = guarded([&] {
        // capture validates state AND every ancestor before inventory/read. An
        // ancillary failure is never permission to bypass these safety checks.
        const Capture captured = capture(root / "work" / "state");
        entries = captured.entry_value();
        assign(observation, U"inventory", ascii_value("complete"));
        const bool complete = std::all_of(captured.entries.begin(), captured.entries.end(), [](const CapturedEntry& e) { return e.type == "file"; });
        assign(observation, U"byte_capture", ascii_value(complete ? "complete" : "partial"));
        std::set<std::u32string> baseline_names, actual_names;
        for (const Value& e : journal.at(U"baseline_members").array) baseline_names.insert(text(e, U"path"));
        for (const auto& e : captured.entries) actual_names.insert(e.path);
        for (const auto& name : baseline_names)
            if (!actual_names.count(name)) {
                Value d = code_only("missing-state-member");
                d.object.emplace_back(U"path", string_value(name));
                diagnostics.array.push_back(std::move(d));
            }
        for (const auto& name : actual_names)
            if (!baseline_names.count(name)) {
                Value d = code_only("unexpected-state-member");
                d.object.emplace_back(U"path", string_value(name));
                diagnostics.array.push_back(std::move(d));
            }
        for (const auto& e : captured.entries)
            if (e.type != "file") {
                Value d = code_only("unsafe-state-entry");
                d.object.emplace_back(U"path", string_value(e.path));
                d.object.emplace_back(U"type", ascii_value(e.type));
                d.object.emplace_back(U"retained_in", ascii_value("work/state"));
                diagnostics.array.push_back(std::move(d));
            }
        // Freeze membership/hash observations before copying. Recovery cannot
        // silently replace an earlier capture with later different bytes.
        if (!journal.contains(U"return_capture")) journal.object.emplace_back(U"return_capture", *entries);
        // Retain ancillary/process findings across a crash during partial copy.
        assign(journal, U"diagnostics", diagnostics);
        session_journal::persist(root, journal);
        if (!schema::equal(journal.at(U"return_capture"), *entries))
            throw StoreError("returned state changed since durable capture; original capture retained");
        const fs::path destination = root / "returned";
        store_paths::safe_path(destination);
        if (fs::exists(destination)) {
            const Capture existing = capture(destination);
            const auto current = blob_map(captured.blobs);
            bool changed = !file_or_directory(existing.entries);
            for (const auto& blob : existing.blobs) {
                const auto found = current.find(blob.path);
                changed = changed || found == current.end() || *found->second != blob.bytes;
            }
            if (changed) throw StoreError("existing returned evidence changed; never overwritten");
        }
        store_write::write_blobs(destination, captured.blobs);
        const Capture copied = capture(destination);
        if (!same_blobs(copied.blobs, captured.blobs) || !file_or_directory(copied.entries))
            throw StoreError("returned evidence copy did not verify");
        assign(observation, U"retained_capture", ascii_value("verified"));
        const Value codec = probe_process::codec_evidence(probe);
        if (!schema::equal(codec, pins.at(U"codec"))) throw StoreError("codec helper evidence changed");
        const Value& slot = pins.at(U"native_slot");
        const Value inspection = probe_process::inspect_bytes(copied.blobs, slot.integer[0] - '0', path_of(codec.at(U"path")));
        decoded = inspection.at(U"decoded");
        assign(observation, U"codec_inspection", ascii_value("complete"));
        for (const Value& finding : inspection.at(U"diagnostics").array) diagnostics.array.push_back(finding);
    })) diagnostics.array.push_back(with_message("return-review-required", *failure));
    const bool clean = diagnostics.array.empty();
    std::map<std::u32string, Value> expected;
    for (const Value& e : journal.at(U"baseline_members").array) expected[text(e, U"path")] = get(e, U"sha256");
    const bool comparison = is_text(observation.at(U"byte_capture"), U"complete");
    Value changed;
    if (comparison) {
        std::map<std::u32string, Value> actual;
        for (const Value& e : entries->array) actual[text(e, U"path")] = e.at(U"sha256");
        std::set<std::u32string> names;
        for (const auto& [name, _] : expected) names.insert(name);
        for (const auto& [name, _] : actual) names.insert(name);
        changed = array_value();
        for (const auto& name : names) {
            const auto x = expected.find(name), y = actual.find(name);
            if (!schema::equal(x == expected.end() ? Value{} : x->second, y == actual.end() ? Value{} : y->second))
                changed.array.push_back(string_value(name));
        }
    }
    std::string readable = "unknown";
    if (decoded) {
        std::set<std::u32string> actual_names, expected_names, decoded_names;
        for (const Value& e : entries->array) actual_names.insert(text(e, U"path"));
        for (const auto& [name, _] : expected) expected_names.insert(name);
        bool missing = false, unreadable = false;
        for (const auto& name : expected_names) missing = missing || !actual_names.count(name);
        if (decoded->kind != Kind::object) throw std::invalid_argument("decoded observation is not an object");
        for (const auto& [name, value] : decoded->object) {
            decoded_names.insert(name);
            if (value.kind != Kind::object) throw std::invalid_argument("codec helper result is not an object");
            unreadable = unreadable || !schema::truth(get(value, U"codec_roundtrip_exact"));
        }
        if (missing || unreadable) readable = "no";
        else if (std::includes(decoded_names.begin(), decoded_names.end(), expected_names.begin(), expected_names.end()))
            readable = decoded_names == expected_names ? "yes" : "no";
        // Present but unsafe/uncaptured expected members remain unknown.
    }
    assign(*member(journal, U"capabilities"), U"returned_native_state_readable", ascii_value(readable));
    assign(journal, U"diagnostics", diagnostics);
    Value reconciliation = object_value();
    reconciliation.object = {{U"status", ascii_value(clean ? "clean-candidate" : "review-required")},
        {U"authority", ascii_value(schema == 4 ? "managed-state-history" : "original-managed-snapshot")},
        {U"promotion", ascii_value(schema == 4 ? "explicit-only" : "deferred")}, {U"observation", observation},
        {U"comparison_status", ascii_value(comparison ? "complete" : "unavailable")}, {U"changed_members", changed},
        {U"pair_atomicity", ascii_value("unverified")}, {U"progression_semantics", ascii_value("unverified")}};
    session_journal::transition(root, journal, clean ? "candidate" : "quarantined",
        {{U"reconciled_at", ascii_value(store_write::now())}, {U"returned_members", entries ? *entries : Value{}},
         {U"returned_observation", decoded ? *decoded : Value{}}, {U"reconciliation", std::move(reconciliation)}});
    return journal;
}

Value reconcile_session(const Store& store, std::u32string_view identity, const std::optional<fs::path>& probe,
                        const session_policy::Policies& policies) {
    store_write::WriterLock lock(store.directory());
    const fs::path root = store_write::session_root(store, identity);
    Value journal = session_journal::read(store, identity);
    const std::u32string& state = text(journal, U"state");
    if (state != U"returned" && state != U"inspecting" && state != U"candidate" && state != U"quarantined")
        throw StoreError("only a durably returned session may be reconciled");
    Value result = reconcile_locked(store, root, journal, probe, policies);
    lock.release();
    return result;
}
} // namespace c2::frontend::reconciliation
