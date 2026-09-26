// Workstream C: lodge.acceptance. See acceptance.hpp. Every check keeps the
// reference order and message; the manifest is the sole authority and the
// snapshot is published before lodge.json commits head + receipt together.
#include "acceptance.hpp"
#include "c2/frontend/core.hpp"
#include "c2/frontend/planning.hpp"
#include "capture.hpp"
#include "content_internal.hpp"
#include "manifest_access.hpp"
#include "manifest_schema.hpp"
#include "planning_internal.hpp"
#include "probe_process.hpp"
#include "schema_compat.hpp"
#include "session_journal.hpp"
#include "sessions.hpp"
#include "store_paths.hpp"
#include <algorithm>
#include <system_error>
namespace fs = std::filesystem;
namespace c2::frontend::acceptance {
using compat::Kind;
using namespace planning_internal;
namespace {
constexpr std::u32string_view AUTHORITY = U"managed-state-history";      // managed_state.AUTHORITY
constexpr std::u32string_view KIND = U"managed-native-hunt-v1";          // native_continuation.KIND
constexpr std::u32string_view POLICY_ID = U"genesis-current-mee-hunt-v1"; // genesis_hunt.POLICY_ID
constexpr std::string_view LOG_LIMIT = "65536";                          // session_runner.LOG_LIMIT
// Reference KeyError/AttributeError over retained evidence of the wrong shape.
struct Malformed : std::runtime_error { using std::runtime_error::runtime_error; };
void require(bool condition, const char* reason) { if (!condition) throw StoreError(reason); }
// dict[key]: a missing key or a non-dict container is malformed evidence.
const Value& item(const Value& v, std::u32string_view key) {
    if (v.kind != Kind::object) throw Malformed("object is not subscriptable");
    if (!v.contains(key)) throw Malformed("missing key");
    return v.at(key);
}
Value* item_ptr(Value& v, std::u32string_view key) {
    for (auto& m : v.object) if (m.first == key) return &m.second;
    return nullptr;
}
// dict.get(key): None when absent; only a dict has .get.
const Value& get(const Value& v, std::u32string_view key) {
    static const Value null;
    if (v.kind != Kind::object) throw Malformed("object has no attribute 'get'");
    return v.contains(key) ? v.at(key) : null;
}
void assign(Value& object, std::u32string_view key, Value value) {
    if (auto* existing = item_ptr(object, key)) { *existing = std::move(value); return; }
    object.object.emplace_back(std::u32string(key), std::move(value));
}
std::u32string widen(std::string_view ascii) { return std::u32string(ascii.begin(), ascii.end()); }
// Native error text (UTF-8) as code points for a diagnostic message.
std::u32string points(std::string_view text) {
    std::u32string out;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        char32_t value = n == 1 ? c : n == 2 ? c & 31 : n == 3 ? c & 15 : c & 7;
        bool valid = n != 0 && i + n <= text.size();
        for (std::size_t k = 1; valid && k < n; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xc0) != 0x80) valid = false; else value = (value << 6) | (next & 63);
        }
        if (!valid) { out.push_back(0xfffd); ++i; continue; }
        out.push_back(value);
        i += n;
    }
    return out;
}
std::string narrow(std::u32string_view text) {
    std::string out;
    for (const char32_t c : text) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    return out;
}
bool is_int(const Value& v) { return v.kind == Kind::integer; }
bool non_negative(const Value& v) { return is_int(v) && v.integer[0] != '-'; }
bool positive(const Value& v) { return non_negative(v) && v.integer != "0"; }
bool nonempty_text(const Value& v) { return v.kind == Kind::string && !v.string.empty(); }
bool same_blobs(const std::vector<CapturedBlob>& a, const std::vector<CapturedBlob>& b) {
    if (a.size() != b.size()) return false;
    for (const auto& x : a) {
        bool found = false;
        for (const auto& y : b) if (x.path == y.path) { found = x.bytes == y.bytes; break; }
        if (!found) return false;
    }
    return true;
}
fs::path units(std::u32string_view text) { return content_internal::native_units(text); }
fs::path uuid_path(std::u32string_view id) { return fs::path(narrow(id)); }
// The reference forms str(path) of the pinned codec/executable evidence.
fs::path path_of(const Value& v) {
    if (v.kind != Kind::string) throw std::invalid_argument("expected str, bytes or os.PathLike object");
    return units(v.string);
}
std::optional<std::u32string> text_arg(std::u32string_view s) { return std::u32string(s); }
// managed_state.resolve_generation over the retained manifest value: no
// fallback, the snapshot must verify against its record.
void resolve_generation(const Store& store, const Value& data, const Value& a,
                        const std::optional<std::u32string>& identity = std::nullopt) {
    const Value& id = item(a, U"id");
    if (id.kind != Kind::string) throw Malformed("association id");
    ManifestAccess::snapshot(store, data).resolve_generation(id.string, identity);
}
struct Candidate { Value* association; Value members; std::vector<CapturedBlob> blobs; };
Candidate validate_candidate_checks(const Store& store, Value& data, const Value& j, std::u32string_view expected,
                                    const std::optional<fs::path>& probe, const session_policy::Policies& policies) {
    require(schema::equal(item(data, U"schema_version"), integer_value("2")), "explicit managed-state schema upgrade required");
    require(schema::equal(item(j, U"schema_version"), integer_value("4")) && is_text(item(item(j, U"execution"), U"kind"), KIND),
            "only generation-pinned native normal-hunt candidates are eligible");
    require(is_text(item(j, U"state"), U"candidate") && !schema::truth(item(j, U"diagnostics")),
            "candidate has unresolved failure/review conditions");
    const Value& pins = item(j, U"pins");
    const Value& association_id = item(pins, U"association_id");
    Value* a = nullptr;
    if (association_id.kind == Kind::string) a = item_ptr(*item_ptr(data, U"associations"), association_id.string);
    require(a && is_text(item(*a, U"authority"), AUTHORITY), "association is not managed history");
    require(schema::valid_id(expected) && is_text(item(item(*a, U"managed_state"), U"current_generation"), expected)
            && is_text(item(pins, U"generation_id"), expected), "candidate predecessor is stale or mismatched");
    // process = j.get('process') or {}
    const Value& raw_process = get(j, U"process");
    static const Value empty_object = object_value();
    const Value& process = schema::truth(raw_process) ? raw_process : empty_object;
    require(is_int(get(process, U"exit_code")) && process.at(U"exit_code").integer == "0"
            && is_text(get(process, U"stop_reason"), U"exited")
            && positive(get(process, U"pid"))
            && schema::equal(get(process, U"started_at"), get(j, U"launched_at"))
            && nonempty_text(get(process, U"started_at"))
            && schema::equal(get(process, U"returned_at"), get(j, U"returned_at"))
            && nonempty_text(get(process, U"returned_at")), "no clean owned process return");
    {
        const std::u32string_view lifecycle[] = {U"prepared", U"launching", U"running", U"returned", U"inspecting", U"candidate"};
        const Value& transitions = item(j, U"transitions");
        if (transitions.kind != Kind::array) throw Malformed("transitions");
        bool history = transitions.array.size() == 6;
        for (std::size_t i = 0; history && i < 6; ++i) history = is_text(item(transitions.array[i], U"state"), lifecycle[i]);
        require(history, "incomplete native lifecycle history");
    }
    const Value& capabilities = item(j, U"capabilities");
    {
        const Value& executed = get(capabilities, U"engine_process_executed");
        require(executed.kind == Kind::boolean && executed.boolean
                && is_text(get(capabilities, U"native_hunt_lifecycle"), U"completed")
                && is_text(get(capabilities, U"synthetic_child_lifecycle"), U"not-executed")
                && is_text(get(capabilities, U"returned_native_state_readable"), U"yes")
                && native_session::supported_contract(get(capabilities, U"engine_contract")), "incomplete native capabilities");
    }
    const Value& rec = item(j, U"reconciliation");
    {
        auto observation = object_value();
        observation.object = {{U"inventory", ascii_value("complete")}, {U"byte_capture", ascii_value("complete")},
            {U"retained_capture", ascii_value("verified")}, {U"codec_inspection", ascii_value("complete")}};
        require(is_text(get(rec, U"status"), U"clean-candidate") && is_text(get(rec, U"authority"), AUTHORITY)
                && is_text(get(rec, U"promotion"), U"explicit-only") && is_text(get(rec, U"comparison_status"), U"complete")
                && schema::equal(get(rec, U"observation"), observation), "incomplete saved observations");
    }
    require(is_text(get(pins, U"adapter"), POLICY_ID) && is_text(get(item(pins, U"hunt_policy"), U"adapter"), POLICY_ID),
            "unsupported native policy provenance");
    // Fresh content/association/profile/codec/selection checks; no engine query.
    {
        const Value& id = item(*a, U"id");
        if (id.kind != Kind::string) throw Malformed("association id");
        const auto current = native_session::native_pins(native_session::Adapter::continuation, store, id.string,
            item(pins, U"selection"), probe, item(pins, U"codec"), policies);
        require(schema::equal(current.pins, pins), "association, hunter, slot, content, policy or source pins changed");
    }
    const Value& jid = item(j, U"id");
    if (jid.kind != Kind::string) throw Malformed("journal id");
    const fs::path root = store_write::session_root(store, jid.string);
    const Value& spec = item(j, U"execution");
    const Value evidence = probe_process::executable_evidence(path_of(item(item(spec, U"executable"), U"path")));
    require(schema::equal(evidence, item(spec, U"executable")), "pinned engine build changed");
    {
        const Value& shell = get(spec, U"shell");
        require(native_session::supported_contract(get(spec, U"contract")) && shell.kind == Kind::boolean && !shell.boolean
                && schema::equal(native_session::execution_spec(native_session::Adapter::continuation, root, pins, evidence,
                                                                native_session::capability(), get(spec, U"timeout_seconds")), spec),
                "native execution evidence changed");
    }
    require(!schema::truth(native_session::workspace_findings(root, true)), "workspace/config/output requires review");
    const Capture baseline = capture(root / "baseline");
    const Capture returned = capture(root / "returned");
    const Capture work = capture(root / "work" / "state");
    const Value baseline_members = baseline.entry_value(), members = returned.entry_value(), work_members = work.entry_value();
    require(schema::equal(baseline_members, item(pins, U"source_members")) && schema::equal(item(pins, U"source_members"), item(j, U"baseline_members")),
            "immutable baseline changed");
    const Value& slot = item(pins, U"native_slot");
    if (!is_int(slot) || slot.integer.size() != 1) throw Malformed("native slot");
    const std::u32string stem = U"trophy0" + widen(slot.integer);
    const std::u32string save = stem + U".sav", room = stem + U".sab";
    {
        bool allowed = returned.blobs.size() == 2 && members.array.size() == 2;
        bool has_save = false, has_room = false;
        for (const auto& blob : returned.blobs) { has_save = has_save || blob.path == save; has_room = has_room || blob.path == room; }
        for (const auto& m : members.array) allowed = allowed && is_text(item(m, U"type"), U"file");
        require(allowed && has_save && has_room, "returned native membership is incomplete or unsafe");
    }
    require(schema::equal(members, get(j, U"returned_members")) && schema::equal(get(j, U"returned_members"), get(j, U"return_capture"))
            && schema::equal(get(j, U"return_capture"), work_members) && same_blobs(returned.blobs, work.blobs),
            "candidate bytes changed since durable observation");
    const fs::path codec = path_of(item(item(pins, U"codec"), U"path"));
    const int slot_number = slot.integer[0] - '0';
    const Value before = probe_process::inspect_bytes(baseline.blobs, slot_number, codec);
    const Value after = probe_process::inspect_bytes(returned.blobs, slot_number, codec);
    {
        auto pair = [&](const Value& decoded) {
            return decoded.object.size() == 2 && decoded.contains(save) && decoded.contains(room);
        };
        require(!schema::truth(before.at(U"diagnostics")) && !schema::truth(after.at(U"diagnostics"))
                && pair(before.at(U"decoded")) && pair(after.at(U"decoded")), "native codecs/registration require review");
    }
    require(schema::equal(before.at(U"decoded"), item(pins, U"source_observation"))
            && schema::equal(after.at(U"decoded"), get(j, U"returned_observation")),
            "saved native observations disagree with fresh bytes");
    {
        auto changed = array_value();
        for (const auto& m : members.array) {
            bool in_baseline = false;
            for (const auto& b : baseline_members.array) if (schema::equal(m, b)) { in_baseline = true; break; }
            if (!in_baseline) changed.array.push_back(item(m, U"path"));
        }
        std::sort(changed.array.begin(), changed.array.end(), [](const Value& x, const Value& y) { return x.string < y.string; });
        require(schema::equal(changed, get(rec, U"changed_members")), "saved native comparison is incomplete");
    }
    const Capture logs = capture(root / "logs");
    {
        bool names = logs.entries.size() == 2;
        bool out = false, err = false;
        for (const auto& e : logs.entries) { out = out || e.path == U"stdout.log"; err = err || e.path == U"stderr.log"; }
        require(names && out && err, "owned process log evidence is incomplete");
    }
    for (const Value& member : logs.entry_value().array) {
        const std::u32string& path = item(member, U"path").string;
        const Value& log = item(item(j, U"logs"), path.substr(0, path.find(U'.')));
        const Value& total = get(log, U"total_bytes");
        const Value& retained = get(log, U"retained_bytes");
        const Value& truncated = get(log, U"truncated");
        bool valid = non_negative(total) && is_int(retained);
        if (valid) {
            const bool over = compare_decimal(total.integer, LOG_LIMIT) > 0;
            valid = compare_decimal(retained.integer, over ? std::string(LOG_LIMIT) : total.integer) == 0
                    && truncated.kind == Kind::boolean && truncated.boolean == over;
        }
        require(valid && is_text(item(member, U"type"), U"file") && schema::equal(item(member, U"size"), retained)
                && is_text(item(log, U"path"), U"logs/" + path) && log.contains(U"error") && log.at(U"error").kind == Kind::null,
                "owned process log capture requires review");
    }
    return {a, members, returned.blobs};
}
// validate_candidate: editable candidate status alone grants nothing.
Candidate validate_candidate(const Store& store, Value& data, const Value& j, std::u32string_view expected,
                             const std::optional<fs::path>& probe, const session_policy::Policies& policies) {
    try {
        return validate_candidate_checks(store, data, j, expected, probe, policies);
    } catch (const Malformed&) { throw StoreError("incomplete or malformed candidate evidence");
    } catch (const std::invalid_argument&) { throw StoreError("incomplete or malformed candidate evidence");
    } catch (const compat::Error&) { throw StoreError("incomplete or malformed candidate evidence"); }
}
struct Found { Value* association; Value* receipt; };
std::optional<Found> find_receipt(Value& data, std::u32string_view session_id) {
    require(schema::valid_id(session_id), "invalid session UUID");
    std::vector<Found> found;
    for (auto& entry : item_ptr(data, U"associations")->object) {
        Value& a = entry.second;
        if (!is_text(get(a, U"authority"), AUTHORITY)) continue;
        Value* history = item_ptr(a, U"managed_state");
        Value* receipts = history && history->kind == Kind::object ? item_ptr(*history, U"receipts") : nullptr;
        if (!receipts) throw Malformed("managed_state receipts");
        if (receipts->kind != Kind::object) throw std::invalid_argument("argument of type is not iterable");
        if (Value* receipt = item_ptr(*receipts, session_id)) found.push_back({&a, receipt});
    }
    require(found.size() <= 1, "ambiguous committed session receipt");
    if (found.empty()) return std::nullopt;
    return found.front();
}
// A convenience copy only. Its absence never rolls back manifest authority.
void complete_receipt(const Store& store, const Value& receipt, const store_write::FailureHook& hook) {
    const Value& session_id = item(receipt, U"session_id");
    if (session_id.kind != Kind::string) throw Malformed("receipt session id");
    const fs::path path = store_write::session_root(store, session_id.string) / "acceptance.json";
    store_paths::safe_path(path);
    const std::string content = session_journal::encode(receipt);
    if (const auto existing = store_paths::read(path, ReadPolicy{})) {
        require(*existing == content, "receipt copy differs; retained for review");
    } else store_write::atomic_write(path, content, hook);
}
Value existing(const Store& store, Value& data, const Found& found, std::u32string_view expected,
               std::u32string_view digest, const store_write::FailureHook& hook) {
    const Value& receipt = *found.receipt;
    require(is_text(item(receipt, U"predecessor"), expected) && is_text(item(receipt, U"candidate_sha256"), digest),
            "retry does not identify the committed candidate/predecessor");
    resolve_generation(store, data, *found.association); // A corrupt current head never silently rolls back.
    const Value& generation = item(receipt, U"generation_id");
    if (generation.kind != Kind::string) throw StoreError("unknown managed-state generation");
    resolve_generation(store, data, *found.association, text_arg(generation.string));
    complete_receipt(store, receipt, hook);
    auto result = object_value();
    result.object.emplace_back(U"result", ascii_value("already-accepted"));
    result.object.emplace_back(U"receipt", receipt);
    result.object.emplace_back(U"current_generation", item(item(*found.association, U"managed_state"), U"current_generation"));
    return result;
}
Value provenance(const Value& a) {
    auto v = object_value();
    for (const auto key : {U"id", U"hunter_id", U"instance_id", U"filename_slot", U"state_key", U"origin", U"revision"})
        v.object.emplace_back(key, item(a, key));
    return v;
}
void mkdir_all(const fs::path& path) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error) throw fs::filesystem_error("mkdir", path, error);
}
}

std::string candidate_digest(const Value& journal) { return sha256(session_journal::encode(journal)); }

Value preview_acceptance(const Store& store, std::u32string_view identity, std::u32string_view expected_generation,
                         const std::optional<fs::path>& probe, const session_policy::Policies& policies) {
    auto result = object_value();
    result.object.emplace_back(U"kind", ascii_value("acceptance-preview-v1"));
    result.object.emplace_back(U"session_id", string_value(std::u32string(identity)));
    result.object.emplace_back(U"expected_generation", string_value(std::u32string(expected_generation)));
    result.object.emplace_back(U"allowed", boolean_value(false));
    result.object.emplace_back(U"diagnostics", array_value());
    auto blocked = [&](const std::string& message) {
        auto d = object_value();
        d.object.emplace_back(U"code", ascii_value("acceptance-blocked"));
        d.object.emplace_back(U"message", string_value(points(message)));
        item_ptr(result, U"diagnostics")->array.push_back(std::move(d));
        assign(result, U"status", ascii_value("blocked"));
        return result;
    };
    try {
        Value data = ManifestAccess::data(store.read());
        const Value j = session_journal::read(store, identity);
        const Value& pins = item(j, U"pins");
        // result.update(**fields): every field is evaluated before any is added.
        std::vector<std::pair<std::u32string, Value>> fields;
        fields.emplace_back(U"association_id", item(pins, U"association_id"));
        fields.emplace_back(U"hunter_id", item(pins, U"hunter_id"));
        fields.emplace_back(U"instance_id", item(pins, U"instance_id"));
        fields.emplace_back(U"native_slot", item(pins, U"native_slot"));
        fields.emplace_back(U"revision", item(pins, U"revision"));
        fields.emplace_back(U"policy", get(pins, U"adapter"));
        fields.emplace_back(U"execution", item(j, U"execution"));
        fields.emplace_back(U"baseline_members", item(j, U"baseline_members"));
        fields.emplace_back(U"returned_members", get(j, U"returned_members"));
        auto observed = object_value();
        observed.object.emplace_back(U"before", get(pins, U"source_observation"));
        observed.object.emplace_back(U"after", get(j, U"returned_observation"));
        {
            // j.get('reconciliation', {}).get(...): a non-dict has no .get (AttributeError propagates).
            const Value& rec = get(j, U"reconciliation");
            if (rec.kind != Kind::null && rec.kind != Kind::object) throw std::invalid_argument("'reconciliation' object has no attribute 'get'");
            observed.object.emplace_back(U"changed_members", rec.kind == Kind::object ? get(rec, U"changed_members") : Value());
        }
        fields.emplace_back(U"observed_changes", std::move(observed));
        fields.emplace_back(U"session_diagnostics", item(j, U"diagnostics"));
        fields.emplace_back(U"candidate_sha256", ascii_value(candidate_digest(j)));
        for (auto& field : fields) assign(result, field.first, std::move(field.second));
        if (const auto committed = find_receipt(data, identity)) {
            resolve_generation(store, data, *committed->association);
            assign(result, U"receipt", *committed->receipt);
            assign(result, U"status", ascii_value("already-accepted"));
            return result;
        }
        validate_candidate(store, data, j, expected_generation, probe, policies);
        assign(result, U"allowed", boolean_value(true));
        assign(result, U"status", ascii_value("eligible"));
    } catch (const ResourceExhausted&) { throw;
    } catch (const StoreError& e) { return blocked(e.what());
    } catch (const planning::Error& e) { return blocked(e.what());          // policy FrontendError
    } catch (const probe_process::ProbeError& e) { return blocked(e.what()); // codec FrontendError
    } catch (const fs::filesystem_error& e) { return blocked(e.what());
    } catch (const Malformed& e) { return blocked(e.what());
    } catch (const std::invalid_argument& e) {
        // Only TypeError is caught by the reference; the AttributeError analogue above propagates.
        if (std::string_view(e.what()).find("has no attribute") != std::string_view::npos) throw;
        return blocked(e.what());
    }
    return result;
}

Value accept_candidate(const Store& store, std::u32string_view identity, std::u32string_view expected_generation,
                       std::u32string_view expected_candidate_sha256, const std::optional<fs::path>& probe,
                       const session_policy::Policies& policies, const store_write::FailureHook& hook) {
    const fs::path manifest = store.directory() / "lodge.json";
    store_paths::safe_path(store.directory()); store_paths::safe_path(manifest);
    store_write::WriterLock lock(store.directory(), hook);
    Value data = ManifestAccess::data(store.read());
    if (const auto found = find_receipt(data, identity)) {
        Value result = existing(store, data, *found, expected_generation, expected_candidate_sha256, hook);
        lock.release();
        return result;
    }
    const Value j = session_journal::read(store, identity);
    require(widen(candidate_digest(j)) == expected_candidate_sha256, "candidate differs from explicit preview");
    Candidate candidate = validate_candidate(store, data, j, expected_generation, probe, policies);
    Value& a = *candidate.association;
    const std::u32string& association_id = item(a, U"id").string;
    const std::string gid = store_write::new_id(), created = store_write::now();
    const fs::path parent = store.directory() / "generations" / uuid_path(association_id);
    store_paths::safe_path(parent);
    mkdir_all(parent);
    const fs::path stage = parent / (".pending-" + gid), final = parent / gid;
    store_paths::safe_path(stage); store_paths::safe_path(final);
    {
        std::error_code error;
        if (!fs::create_directory(stage, error) || error)
            throw fs::filesystem_error("mkdir", stage, error ? error : std::make_error_code(std::errc::file_exists));
    }
    store_write::write_blobs(stage, candidate.blobs, hook);
    {
        const Capture staged = capture(stage);
        require(schema::equal(staged.entry_value(), candidate.members) && same_blobs(staged.blobs, candidate.blobs),
                "staged native bytes did not verify");
    }
    require(!fs::exists(final), "generation identity already exists");
    {
        std::error_code error;
        fs::rename(stage, final, error);
        if (error) throw fs::filesystem_error("rename", stage, final, error);
    }
    // The verified snapshot and all newly created ancestors precede the head.
    for (const fs::path& directory : {final, parent, parent.parent_path(), store.directory()}) store_write::sync_directory(directory);
    {
        const Capture published = capture(final);
        require(schema::equal(published.entry_value(), candidate.members) && same_blobs(published.blobs, candidate.blobs),
                "published snapshot did not verify");
    }
    const Value& pins = j.at(U"pins");
    auto receipt = object_value();
    receipt.object = {{U"schema_version", integer_value("1")}, {U"association_id", string_value(association_id)},
        {U"session_id", string_value(std::u32string(identity))}, {U"generation_id", ascii_value(gid)},
        {U"predecessor", string_value(std::u32string(expected_generation))}, {U"members", candidate.members},
        {U"revision", pins.at(U"revision")}, {U"policy", string_value(std::u32string(POLICY_ID))},
        {U"execution", j.at(U"execution")}, {U"accepted_at", ascii_value(created)},
        {U"candidate_sha256", string_value(std::u32string(expected_candidate_sha256))},
        {U"acceptance", ascii_value("explicit")}};
    Value& h = *item_ptr(a, U"managed_state");
    Value& generations = *item_ptr(h, U"generations");
    const Value& previous = item(generations, expected_generation);
    auto record = object_value();
    record.object = {{U"id", ascii_value(gid)}, {U"sequence", integer_value(add_decimal(item(previous, U"sequence").integer, "1"))},
        {U"predecessor", string_value(std::u32string(expected_generation))}, {U"source_session", string_value(std::u32string(identity))},
        {U"kind", ascii_value("accepted-native-hunt")}, {U"created_at", ascii_value(created)}, {U"members", candidate.members},
        {U"provenance", provenance(a)}, {U"snapshot", string_value(U"generations/" + association_id + U"/" + widen(gid))},
        {U"policy", string_value(std::u32string(POLICY_ID))}, {U"revision", pins.at(U"revision")}, {U"execution", j.at(U"execution")}};
    assign(generations, widen(gid), std::move(record));
    assign(*item_ptr(h, U"receipts"), identity, receipt);
    assign(h, U"current_generation", ascii_value(gid));
    try { schema::validate_manifest(data); } catch (const compat::Error& e) { throw StoreError(e.what()); }
    {
        const fs::path backup = store.directory() / "lodge.json.bak";
        store_paths::safe_path(backup);
        const auto current = store_paths::read(manifest, ReadPolicy{});
        if (!current) throw fs::filesystem_error("read", manifest, std::make_error_code(std::errc::no_such_file_or_directory));
        store_write::atomic_write(backup, *current, hook);
    }
    // THE COMMIT POINT: new head and authoritative receipt become visible together.
    store_write::atomic_write(manifest, session_journal::encode(data), hook);
    complete_receipt(store, receipt, hook);
    auto result = object_value();
    result.object.emplace_back(U"result", ascii_value("accepted"));
    result.object.emplace_back(U"receipt", std::move(receipt));
    result.object.emplace_back(U"current_generation", ascii_value(gid));
    lock.release();
    return result;
}

Value recover_acceptance(const Store& store, std::u32string_view identity, const store_write::FailureHook& hook) {
    store_paths::safe_path(store.directory()); store_paths::safe_path(store.directory() / "lodge.json");
    store_write::WriterLock lock(store.directory(), hook);
    Value data = ManifestAccess::data(store.read());
    Value result;
    if (const auto found = find_receipt(data, identity)) {
        const Value& receipt = *found->receipt;
        const Value& predecessor = item(receipt, U"predecessor");
        const Value& digest = item(receipt, U"candidate_sha256");
        if (predecessor.kind != Kind::string || digest.kind != Kind::string) throw Malformed("receipt identity");
        result = existing(store, data, *found, predecessor.string, digest.string, hook);
    } else {
        result = object_value();
        result.object.emplace_back(U"result", ascii_value("not-committed"));
        result.object.emplace_back(U"session_id", string_value(std::u32string(identity)));
        result.object.emplace_back(U"action", ascii_value("evidence-retained-no-promotion"));
    }
    lock.release();
    return result;
}
}
