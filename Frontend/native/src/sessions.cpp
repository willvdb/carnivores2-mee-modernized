// Workstream A: lodge.sessions (synthetic preparation) and lodge.native_session
// with the native_observer / native_hunt / native_continuation adapters.
#include "sessions.hpp"
#include "c2/frontend/catalog.hpp"
#include "c2/frontend/core.hpp"
#include "capture.hpp"
#include "catalog_internal.hpp"
#include "content_internal.hpp"
#include "planning_internal.hpp"
#include "probe_process.hpp"
#include "schema_compat.hpp"
#include "session_journal.hpp"
#include "store_paths.hpp"
#include "store_write.hpp"
#include <algorithm>
#include <chrono>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace fs = std::filesystem;
namespace c2::frontend {
namespace {
using compat::Kind;
using compat::Value;
using namespace planning_internal;
const std::u32string& text(const Value& object, std::u32string_view key) {
    const Value& v = object.at(key);
    if (v.kind != Kind::string) throw std::invalid_argument("retained field is not a string");
    return v.string;
}
Value* member(Value& object, std::u32string_view key) {
    if (object.kind != Kind::object) return nullptr;
    for (auto& m : object.object) if (m.first == key) return &m.second;
    return nullptr;
}
// dict.update semantics: an existing key keeps its position, a new one is appended.
void assign(Value& object, std::u32string_view key, Value value) {
    if (Value* existing = member(object, key)) { *existing = std::move(value); return; }
    object.object.emplace_back(std::u32string(key), std::move(value));
}
Value path_value(const fs::path& path) { return string_value(store_paths::native_points(path)); }
fs::path path_of(const Value& value) {
    if (value.kind != Kind::string) throw std::invalid_argument("retained path is not a string");
    return content_internal::native_units(value.string);
}
bool lower_sha256(const std::u32string& s) {
    if (s.size() != 64) return false;
    for (const char32_t c : s) if (!((c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'f'))) return false;
    return true;
}
// type(x) in (int, float) and lo <= x <= hi. An int compares exactly against
// the decimal bounds (the fractional low bound rounds up to the next integer).
bool timeout_within(const Value& v, std::string_view lo_int, std::string_view hi_int, double lo, double hi) {
    if (v.kind == Kind::integer) return compare_decimal(v.integer, lo_int) >= 0 && compare_decimal(v.integer, hi_int) <= 0;
    if (v.kind == Kind::floating) return v.floating >= lo && v.floating <= hi; // NaN compares false
    return false;
}
// PurePath.is_relative_to over traversal-free paths: pathlib's parts without
// empty/'.' components, compared case-insensitively on NT (normcase).
std::vector<std::u32string> lexical_parts(const fs::path& path) {
    std::vector<std::u32string> out;
    for (const auto& part : path) {
        auto s = store_paths::native_points(part);
        if (s.empty() || s == U".") continue;
#ifdef _WIN32
        s = schema::lower(s);
#endif
        out.push_back(std::move(s));
    }
    return out;
}
bool relative_to(const fs::path& path, const fs::path& other) {
    const auto a = lexical_parts(path), b = lexical_parts(other);
    return b.size() <= a.size() && std::equal(b.begin(), b.end(), a.begin());
}
std::u32string decimal_text(const Value& v) {
    if (v.kind != Kind::integer) throw std::invalid_argument("pinned native slot is not an integer");
    return std::u32string(v.integer.begin(), v.integer.end());
}
// Path.mkdir(parents=True, exist_ok=False) and Path.mkdir().
void make_new_directory(const fs::path& path, bool parents) {
    std::error_code error;
    if (fs::exists(path, error) || error)
        throw fs::filesystem_error("cannot create directory", path, error ? error : std::make_error_code(std::errc::file_exists));
    if (parents) fs::create_directories(path);
    else fs::create_directory(path);
}
Value transitions_value(const std::string& state, const std::string& at) {
    Value event = object_value();
    event.object.emplace_back(U"state", ascii_value(state));
    event.object.emplace_back(U"at", ascii_value(at));
    Value list = array_value();
    list.array.push_back(std::move(event));
    return list;
}
Value reconciliation_value(std::string_view authority, std::string_view promotion) {
    Value v = object_value();
    v.object = {{U"authority", ascii_value(authority)}, {U"promotion", ascii_value(promotion)}, {U"status", ascii_value("pending")}};
    return v;
}
fs::path running_executable() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n == 0 || n >= buffer.size()) throw StoreError("cannot locate the running executable");
    buffer.resize(n);
    return fs::path(buffer);
#elif defined(__linux__)
    std::error_code error;
    const fs::path exe = fs::read_symlink("/proc/self/exe", error);
    if (error) throw StoreError("cannot locate the running executable");
    return exe;
#else
    throw StoreError("cannot locate the running executable");
#endif
}
} // namespace

namespace sessions {
const std::vector<std::u32string>& scenarios() {
    static const std::vector<std::u32string> list{U"unchanged", U"sav", U"pair", U"nonzero", U"changed-nonzero",
        U"corrupt-sav", U"corrupt-sab", U"missing-sab", U"deleted-sav", U"extra", U"registration", U"hang",
        U"terminated", U"logs"};
    return list;
}
fs::path synthetic_fixture() {
    fs::path path = running_executable().parent_path() / "c2-frontend-synthetic-child";
#ifdef _WIN32
    path += ".exe";
#endif
    return path;
}
Value execution_spec(const fs::path& root, const Value& scenario, const Value& slot, const Value& timeout) {
    const auto& known = scenarios();
    if (scenario.kind != Kind::string || std::find(known.begin(), known.end(), scenario.string) == known.end())
        throw StoreError("unknown controlled synthetic scenario");
    if (!timeout_within(timeout, "1", "30", 0.05, 30))
        throw StoreError("synthetic timeout must be between 0.05 and 30 seconds");
    // The compiled child is both the interpreter and the fixture of the
    // reference spec; its evidence is never read from a journal.
    const Value executable = probe_process::executable_evidence(synthetic_fixture());
    Value argv = array_value();
    argv.array = {executable.at(U"path"), ascii_value("--scenario"), scenario, ascii_value("--slot"),
        string_value(decimal_text(slot)), ascii_value("--literal"), ascii_value(LITERAL_ARGUMENT)};
    Value spec = object_value();
    spec.object = {{U"kind", ascii_value("controlled-synthetic")}, {U"executable", executable}, {U"fixture", executable},
        {U"argv", std::move(argv)}, {U"cwd", path_value(root / "work")}, {U"timeout_seconds", timeout},
        {U"scenario", scenario}, {U"shell", boolean_value(false)}};
    return spec;
}
Value prepare_session(const Store& store, std::u32string_view association_id, std::u32string area,
                      std::u32string scenario, const Value& time_of_day, const Value& timeout,
                      const std::optional<fs::path>& probe) {
    Value selection = object_value();
    selection.object = {{U"area", string_value(std::move(area))}, {U"mode", ascii_value("observer")},
        {U"time_of_day", time_of_day}, {U"licenses", array_value()}, {U"weapons", array_value()}, {U"equipment", array_value()}};
    store_write::WriterLock lock(store.directory());
    auto captured = planning_store::snapshot_pins(store, association_id, selection, probe);
    const std::string identity = store_write::new_id();
    const fs::path root = store_write::session_root(store, std::u32string(identity.begin(), identity.end()));
    const Value spec = execution_spec(root, string_value(std::move(scenario)), captured.pins.at(U"native_slot"), timeout);
    make_new_directory(root, true);
    store_write::write_blobs(root / "baseline", captured.blobs);
    store_write::write_blobs(root / "work" / "state", captured.blobs);
    make_new_directory(root / "logs", false);
    const std::string created = store_write::now();
    Value capabilities = object_value();
    capabilities.object = {{U"session_workspace", ascii_value("validated")},
        {U"synthetic_child_lifecycle", ascii_value("not-executed")}, {U"genesis_policy", ascii_value("not-evaluated")},
        {U"engine_process_executed", boolean_value(false)}, {U"observer_session_launched", boolean_value(false)},
        {U"returned_native_state_readable", ascii_value("unknown")}, {U"hunt_save_round_trip_validated", boolean_value(false)},
        {U"modern_engine_compatibility", ascii_value("unknown")}};
    Value journal = object_value();
    journal.object = {{U"schema_version", integer_value("1")}, {U"id", ascii_value(identity)},
        {U"path_flavor", string_value(std::u32string(session_journal::path_flavor()))}, {U"state", ascii_value("prepared")},
        {U"created_at", ascii_value(created)}, {U"prepared_at", ascii_value(created)},
        {U"transitions", transitions_value("prepared", created)}, {U"pins", captured.pins},
        {U"baseline_members", captured.pins.at(U"source_members")}, {U"execution", spec},
        {U"diagnostics", array_value()}, {U"process", Value{}},
        {U"reconciliation", reconciliation_value("original-managed-snapshot", "deferred")},
        {U"capabilities", std::move(capabilities)}, {U"process_launch_allowed", boolean_value(false)},
        {U"synthetic_process_launch_allowed", boolean_value(true)}};
    session_journal::persist(root, journal);
#ifndef _WIN32
    // Persist both the UUID directory and a newly created sessions/ entry.
    store_write::sync_directory(root.parent_path());
    store_write::sync_directory(store.directory());
#endif
    lock.release();
    return journal;
}
} // namespace sessions

namespace native_session {
namespace {
std::u32string_view policy_key(Adapter adapter) { return adapter == Adapter::observer ? U"observer_policy" : U"hunt_policy"; }
std::string schema_text(Adapter adapter) { return std::to_string(static_cast<int>(adapter)); }
} // namespace
std::u32string_view kind(Adapter adapter) {
    switch (adapter) {
    case Adapter::observer: return U"experimental-native-observer-v1";
    case Adapter::hunt: return U"experimental-native-hunt-v1";
    case Adapter::continuation: return U"managed-native-hunt-v1";
    }
    throw std::invalid_argument("unknown native adapter");
}
std::u32string_view lifecycle(Adapter adapter) {
    return adapter == Adapter::observer ? U"native_observer_lifecycle" : U"native_hunt_lifecycle";
}
const Value& capability() {
    static const Value value = [] {
        Value v = object_value();
        v.object = {{U"contract", ascii_value("c2-engine-session")}, {U"version", integer_value("1")},
            {U"state", ascii_value("sav-sab-pair")}, {U"layout", ascii_value("state-config-output-v1")},
            {U"performance_capture", boolean_value(false)}};
        return v;
    }();
    return value;
}
const std::string& config() {
    static const std::string bytes = "display_mode 0\nresolution 800x600\nfps_limit 1\nglperf_logging 0\n";
    return bytes;
}
bool supported_contract(const Value& value) {
    // Keep JSON booleans distinct from numeric fields in both query and journals.
    if (value.kind != Kind::object || !schema::equal(value, capability())) return false;
    return value.at(U"version").kind == Kind::integer && value.at(U"performance_capture").kind == Kind::boolean;
}
Value trusted_engine(const fs::path& engine, const Value& digest, bool experimental) {
    if (!experimental) throw StoreError("native execution requires the explicit experimental gate");
    if (digest.kind != Kind::string || !lower_sha256(digest.string))
        throw StoreError("explicit trusted engine SHA-256 is required");
    Value evidence = probe_process::executable_evidence(engine);
    store_paths::safe_path(path_of(evidence.at(U"path")));
    if (!schema::equal(evidence.at(U"sha256"), digest))
        throw StoreError("selected engine does not match the explicitly trusted hash");
    return evidence;
}
Value query_contract(const Value& evidence) {
    // Only after explicit binary trust; capability text is not certification.
    // Bounded helper-style query: the child inherits this process's working
    // directory rather than the reference's fresh temporary directory
    // (documented difference); stdin is closed and both streams are captured.
    const fs::path path = path_of(evidence.at(U"path"));
    probe_process::Result result;
    try {
        result = probe_process::run(path, {"--session-capabilities"}, "", std::chrono::seconds(5));
    } catch (const probe_process::Timeout&) {
        throw StoreError("trusted engine capability query timed out");
    } catch (const probe_process::OutputLimit&) {
        throw StoreError("trusted engine capability query failed");
    }
    if (result.returncode != 0 || !result.err.empty() || result.out.size() > 4096)
        throw StoreError("trusted engine capability query failed");
    Value value;
    try { value = compat::parse(result.out); }
    catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
    catch (const compat::Error&) { throw StoreError("invalid engine capability response"); }
    if (!supported_contract(value)) throw StoreError("unsupported engine session contract");
    if (!schema::equal(probe_process::executable_evidence(path), evidence))
        throw StoreError("engine changed during capability query");
    return value;
}
void disjoint(const fs::path& workspace, const Value& pins, const Value& executable, bool include_source) {
    store_paths::safe_path(workspace);
    std::vector<fs::path> protected_roots{path_of(pins.at(U"instance").at(U"path")), path_of(executable.at(U"path")).parent_path()};
    if (include_source) protected_roots.push_back(path_of(pins.at(U"source_root")));
    for (const auto& protected_root : protected_roots) {
        store_paths::safe_path(protected_root);
        if (relative_to(workspace, protected_root) || relative_to(protected_root, workspace))
            throw StoreError("native workspace overlaps protected source/content/engine");
    }
}
namespace {
// The exact legacy platform screenshot allowlist: carnivor.log | render.log |
// HUNT[0-9]{4}\.BM on NT, HUNT[0-9]{4,}\.BMP elsewhere (ASCII digits, full match).
bool allowed_output(const std::u32string& name) {
    if (name == U"carnivor.log" || name == U"render.log") return true;
    if (name.compare(0, 4, U"HUNT") != 0) return false;
    std::size_t i = 4;
    while (i < name.size() && name[i] >= U'0' && name[i] <= U'9') ++i;
    const std::size_t digits = i - 4;
#ifdef _WIN32
    return digits == 4 && name.compare(i, std::u32string::npos, U".BM") == 0;
#else
    return digits >= 4 && name.compare(i, std::u32string::npos, U".BMP") == 0;
#endif
}
std::vector<std::u32string> entry_names(const fs::path& directory) {
    std::vector<std::u32string> names;
    for (const auto& entry : fs::directory_iterator(directory)) names.push_back(store_paths::native_points(entry.path().filename()));
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}
Value finding(std::string_view domain, const std::string& message) {
    Value v = object_value();
    v.object = {{U"code", ascii_value("native-workspace-review-required")}, {U"domain", ascii_value(domain)},
        {U"message", string_value(store_paths::native_points(fs::u8path(message)))}};
    return v;
}
} // namespace
Value workspace_findings(const fs::path& root, bool returning) {
    // Check ancillary domains independently; never authorize state access here.
    const fs::path work = root / "work";
    store_paths::safe_path(work);
    Value findings = array_value();
    for (const char* domain : {"workspace", "config", "output"}) {
        try {
            if (std::string_view(domain) == "workspace") {
                if (entry_names(work) != std::vector<std::u32string>{U"config", U"output", U"state"})
                    throw StoreError("unexpected native workspace entry");
            } else if (std::string_view(domain) == "config") {
                const Capture configuration = capture(work / "config");
                if (configuration.blobs.size() != 1 || configuration.blobs[0].path != U"config.cfg"
                    || configuration.entries.size() != 1 || configuration.blobs[0].bytes != config())
                    throw StoreError("native session configuration changed");
            } else {
                const Capture output = capture(work / "output");
                if (!returning && !output.entries.empty()) throw StoreError("native output already exists; no relaunch");
                for (const auto& entry : output.entries)
                    if (entry.type != "file" || !allowed_output(entry.path))
                        throw StoreError("unexpected native output; retained for review");
            }
        } catch (const ResourceExhausted&) {
            throw;
        } catch (const StoreError& error) {
            findings.array.push_back(finding(domain, error.what()));
        } catch (const fs::filesystem_error& error) {
            findings.array.push_back(finding(domain, error.what()));
        }
    }
    return findings;
}
planning_store::PinSnapshot native_pins(Adapter adapter, const Store& store, std::u32string_view association_id,
    const Value& selection, const std::optional<fs::path>& probe, const std::optional<Value>& expected_codec,
    const session_policy::Policies& policies, const std::optional<std::u32string>& generation) {
    if (generation && adapter != Adapter::continuation)
        throw std::invalid_argument("generation is only accepted by the continuation adapter");
    auto captured = planning_store::snapshot_pins(store, association_id, selection, probe, expected_codec,
        adapter == Adapter::observer ? U"observer" : U"hunt", adapter == Adapter::continuation, generation);
    // snapshot_pins already requires the canonical SAV and excludes other names.
    if (captured.blobs.size() != 2)
        throw StoreError(adapter == Adapter::observer ? "engine contract v1 requires an existing complete SAV/SAB pair"
                                                      : "hunt contract requires an existing complete SAV/SAB pair");
    const Value& instance = captured.pins.at(U"instance");
    const auto projection = catalog::project(content_internal::native_units(text(instance, U"path")), text(instance, U"dialect_hint"));
    const Value& slot = captured.pins.at(U"native_slot");
    const std::u32string save = U"trophy0" + std::u32string(1, static_cast<char32_t>(slot.integer[0])) + U".sav";
    // A missing score is the reference KeyError, never a fabricated zero.
    const Value& score = captured.pins.at(U"source_observation").at(save).at(U"score");
    const planning_store::PolicyEvaluator& test_policy = adapter == Adapter::observer ? policies.observer : policies.hunt;
    const planning_store::PolicyEvaluator production = adapter == Adapter::observer
        ? planning_store::PolicyEvaluator(observer_policy) : planning_store::PolicyEvaluator(hunt_policy);
    const Value policy = (test_policy ? test_policy : production)(captured.pins.at(U"revision"), projection, slot, selection, score);
    assign(captured.pins, U"adapter", string_value(std::u32string(adapter == Adapter::observer ? planning::OBSERVER_POLICY_ID : planning::HUNT_POLICY_ID)));
    assign(captured.pins, policy_key(adapter), policy);
    return captured;
}
planning_store::PinSnapshot return_pins(const Store& store, const Value& pins, const std::optional<fs::path>& probe,
                                        const session_policy::Policies& policies) {
    // Historical evidence is compared with its actual pinned generation, even
    // after the head advances. Acceptance separately requires the current head.
    return native_pins(Adapter::continuation, store, text(pins, U"association_id"), pins.at(U"selection"), probe,
                       pins.at(U"codec"), policies, text(pins, U"generation_id"));
}
Value execution_spec(Adapter adapter, const fs::path& root, const Value& pins, const Value& evidence,
                     const Value& capability, const Value& timeout) {
    if (!timeout_within(timeout, "30", "3600", 30, 3600))
        throw StoreError("developer native validation timeout must be 30..3600 seconds");
    disjoint(root, pins, evidence, true);
    // The mode-specific policy already validates these arguments. The slot is
    // supplied exclusively by the versioned contract, never the legacy reg= parser.
    const Value& policy_args = pins.at(policy_key(adapter)).at(U"candidate_argv");
    if (policy_args.kind != Kind::array) throw std::invalid_argument("candidate_argv is not a list");
    if (policy_args.array.empty()) throw std::out_of_range("candidate_argv is empty"); // reference IndexError
    const std::u32string slot = decimal_text(pins.at(U"native_slot"));
    if (!is_text(policy_args.array[0], U"reg=" + slot)) throw StoreError("native policy slot disagrees with native state");
    Value argv = array_value();
    argv.array = {evidence.at(U"path"), ascii_value("--session-contract=1"), string_value(U"--session-slot=" + slot),
        string_value(U"--session-root=" + store_paths::native_points(root / "work")),
        string_value(U"--session-source=" + text(pins, U"source_root")),
        string_value(U"--session-baseline=" + store_paths::native_points(root / "baseline"))};
    for (std::size_t i = 1; i < policy_args.array.size(); ++i) argv.array.push_back(policy_args.array[i]);
    Value spec = object_value();
    spec.object = {{U"kind", string_value(std::u32string(kind(adapter)))}, {U"executable", evidence}, {U"contract", capability},
        {U"argv", std::move(argv)}, {U"cwd", pins.at(U"instance").at(U"path")}, {U"shell", boolean_value(false)},
        {U"timeout_seconds", timeout}, {U"config_sha256", ascii_value(sha256(config()))}};
    return spec;
}
Adapter adapter_for(const Value& journal) {
    const Value& version = journal.at(U"schema_version");
    std::optional<Adapter> adapter;
    if (version.kind == Kind::integer) {
        if (version.integer == "2") adapter = Adapter::observer;
        else if (version.integer == "3") adapter = Adapter::hunt;
        else if (version.integer == "4") adapter = Adapter::continuation;
    }
    if (!adapter || !is_text(journal.at(U"execution").at(U"kind"), kind(*adapter)))
        throw StoreError("unsupported native session kind/version");
    return *adapter;
}
Adapter preparation_adapter(const Store& store) {
    return store.read().schema_version() == 2 ? Adapter::continuation : Adapter::hunt;
}
Value prepare(Adapter adapter, const Store& store, std::u32string_view association_id, const Value& selection,
              const fs::path& engine, const Value& digest, bool experimental, const Value& timeout,
              const std::optional<fs::path>& probe, const session_policy::Policies& policies) {
    const Value evidence = trusted_engine(engine, digest, experimental);
    // Read-only validation before creating even the store lock in an unsafe root.
    const auto preview = native_pins(adapter, store, association_id, selection, probe, std::nullopt, policies);
    disjoint(store.directory(), preview.pins, evidence, false);
    store_write::WriterLock lock(store.directory());
    auto captured = native_pins(adapter, store, association_id, selection, probe, std::nullopt, policies);
    const Value contract = query_contract(evidence);
    const std::string identity = store_write::new_id();
    const fs::path root = store_write::session_root(store, std::u32string(identity.begin(), identity.end()));
    const Value spec = execution_spec(adapter, root, captured.pins, evidence, contract, timeout);
    make_new_directory(root, true);
    store_write::write_blobs(root / "baseline", captured.blobs);
    store_write::write_blobs(root / "work" / "state", captured.blobs);
    store_write::write_blobs(root / "work" / "config", {{U"config.cfg", config()}});
    make_new_directory(root / "work" / "output", false);
    make_new_directory(root / "logs", false);
    const std::string created = store_write::now();
    Value capabilities = object_value();
    capabilities.object = {{U"session_workspace", ascii_value("validated")}, {U"engine_contract", contract},
        {U"synthetic_child_lifecycle", ascii_value("not-executed")},
        {std::u32string(lifecycle(adapter)), ascii_value("not-executed")},
        {U"genesis_policy", ascii_value("structurally-validated")}, {U"engine_process_executed", boolean_value(false)},
        {U"observer_session_launched", boolean_value(false)}, {U"returned_native_state_readable", ascii_value("unknown")},
        {U"hunt_save_round_trip_validated", boolean_value(false)}, {U"modern_engine_compatibility", ascii_value("unknown")}};
    Value journal = object_value();
    journal.object = {{U"schema_version", integer_value(schema_text(adapter))}, {U"id", ascii_value(identity)},
        {U"path_flavor", string_value(std::u32string(session_journal::path_flavor()))}, {U"state", ascii_value("prepared")},
        {U"created_at", ascii_value(created)}, {U"prepared_at", ascii_value(created)},
        {U"transitions", transitions_value("prepared", created)}, {U"pins", captured.pins},
        {U"baseline_members", captured.pins.at(U"source_members")}, {U"execution", spec},
        {U"diagnostics", array_value()}, {U"process", Value{}},
        {U"reconciliation", adapter == Adapter::continuation ? reconciliation_value("managed-state-history", "explicit-only")
                                                             : reconciliation_value("original-managed-snapshot", "deferred")},
        {U"process_launch_allowed", boolean_value(false)}, {U"synthetic_process_launch_allowed", boolean_value(false)},
        {U"experimental_native_process_launch_allowed", boolean_value(true)}, {U"capabilities", std::move(capabilities)}};
    session_journal::persist(root, journal);
#ifndef _WIN32
    for (const fs::path& directory : {root / "work", root, root.parent_path(), store.directory()})
        store_write::sync_directory(directory);
#endif
    lock.release();
    return journal;
}
} // namespace native_session
} // namespace c2::frontend
