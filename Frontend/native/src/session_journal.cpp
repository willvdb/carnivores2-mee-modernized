#include "session_journal.hpp"
#include "manifest_schema.hpp"
#include "planning_internal.hpp"
#include "store_paths.hpp"
#include <array>
namespace fs = std::filesystem;
namespace c2::frontend::session_journal {
namespace {
using compat::Kind;
using compat::Value;
struct Edge { std::u32string_view from; std::array<std::u32string_view, 3> to; };
constexpr std::array<Edge, 9> edges{{
    {U"prepared", {U"launching", U"failed", U""}},
    {U"launching", {U"running", U"failed", U"interrupted"}},
    {U"running", {U"returned", U"interrupted", U""}},
    {U"returned", {U"inspecting", U"", U""}},
    {U"inspecting", {U"candidate", U"quarantined", U""}},
    {U"candidate", {U"", U"", U""}}, {U"quarantined", {U"", U"", U""}},
    {U"failed", {U"", U"", U""}}, {U"interrupted", {U"", U"", U""}}}};
constexpr std::array<std::u32string_view, 4> kinds{U"controlled-synthetic", U"experimental-native-observer-v1",
    U"experimental-native-hunt-v1", U"managed-native-hunt-v1"};
const Value* get(const Value& object, std::u32string_view key) {
    return object.contains(key) ? &object.at(key) : nullptr;
}
bool is_kind(const Value* v, Kind k) { return v && v->kind == k; }
bool is_string(const Value* v, std::u32string_view text) { return is_kind(v, Kind::string) && v->string == text; }
bool is_boolean(const Value* v, bool b) { return is_kind(v, Kind::boolean) && v->boolean == b; }
// type(x) is int and lo <= x <= hi over the canonical decimal.
bool integer_between(const Value* v, std::string_view lo, std::string_view hi) {
    return is_kind(v, Kind::integer) && planning_internal::compare_decimal(v->integer, lo) >= 0
        && planning_internal::compare_decimal(v->integer, hi) <= 0;
}
bool lower_sha256(const std::u32string& s) {
    if (s.size() != 64) return false;
    for (const char32_t c : s) if (!((c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'f'))) return false;
    return true;
}
[[noreturn]] void fail(const char* message) { throw StoreError(message); }
}

std::u32string_view path_flavor() noexcept {
#ifdef _WIN32
    return U"nt";
#else
    return U"posix";
#endif
}
bool known_state(std::u32string_view state) noexcept {
    for (const auto& e : edges) if (e.from == state) return true;
    return false;
}
bool transition_allowed(std::u32string_view from, std::u32string_view to) noexcept {
    if (to.empty()) return false;
    for (const auto& e : edges) if (e.from == from) for (const auto t : e.to) if (t == to) return true;
    return false;
}

void validate(const Value& journal, std::u32string_view identity) {
    if (journal.kind != Kind::object) fail("invalid/foreign session journal; explicit review required");
    const Value* version = get(journal, U"schema_version");
    const Value* state = get(journal, U"state");
    if (!integer_between(version, "1", "4") || !is_string(get(journal, U"id"), identity)
        || !schema::valid_id(identity) || !is_string(get(journal, U"path_flavor"), path_flavor())
        || !is_kind(state, Kind::string) || !known_state(state->string))
        fail("invalid/foreign session journal; explicit review required");
    const int schema_version = version->integer[0] - '0';
    const Value* events = get(journal, U"transitions");
    if (!is_kind(events, Kind::array) || events->array.empty() || events->array[0].kind != Kind::object
        || !is_string(get(events->array[0], U"state"), U"prepared"))
        fail("invalid session transition history");
    const std::u32string* previous = nullptr;
    for (const Value& event : events->array) {
        if (event.kind != Kind::object || !is_kind(get(event, U"at"), Kind::string)) fail("invalid session event");
        const Value* next = get(event, U"state");
        if (!is_kind(next, Kind::string) || !known_state(next->string)
            || (previous && !transition_allowed(*previous, next->string)))
            fail("illegal session transition history");
        previous = &next->string;
    }
    if (*previous != state->string) fail("session state disagrees with transition history");
    for (const auto field : {U"pins", U"execution", U"capabilities"})
        if (!is_kind(get(journal, field), Kind::object)) fail("incomplete session journal");
    const Value* kind = get(journal.at(U"execution"), U"kind");
    bool later_kind = false;
    for (std::size_t i = 1; i < kinds.size(); ++i) later_kind = later_kind || is_string(kind, kinds[i]);
    if ((schema_version == 1 && later_kind)
        || (schema_version >= 2 && !is_string(kind, kinds[static_cast<std::size_t>(schema_version - 1)]))
        || (schema_version >= 2 && (!is_boolean(get(journal, U"experimental_native_process_launch_allowed"), true)
            || !is_boolean(get(journal, U"process_launch_allowed"), false)
            || !is_boolean(get(journal, U"synthetic_process_launch_allowed"), false))))
        fail("incompatible session journal kind/version/capability");
    if (!is_kind(get(journal, U"diagnostics"), Kind::array)) fail("invalid session diagnostics");
    const Value& pins = journal.at(U"pins");
    for (const auto field : {U"hunter_id", U"instance_id", U"association_id"}) {
        const Value* id = get(pins, field);
        if (!is_kind(id, Kind::string) || !schema::valid_id(id->string)) fail("invalid session provenance identity");
    }
    for (const auto field : {U"selection", U"codec", U"revision", U"instance", U"association"})
        if (!is_kind(get(pins, field), Kind::object)) fail("incomplete pinned session provenance");
    if (schema_version == 4) {
        const Value* generation_id = get(pins, U"generation_id");
        const Value* generation = get(pins, U"generation");
        if (!is_kind(generation_id, Kind::string) || !schema::valid_id(generation_id->string)
            || !is_kind(generation, Kind::object) || !is_string(get(*generation, U"id"), generation_id->string))
            fail("missing managed generation pin");
    }
    const Value* slot = get(pins, U"native_slot");
    if (!integer_between(slot, "0", "7")) fail("invalid pinned native slot");
    std::u32string save = U"trophy0", room;
    save.push_back(static_cast<char32_t>(slot->integer[0]));
    room = save + U".sab";
    save += U".sav";
    for (const Value* members : {get(pins, U"source_members"), get(journal, U"baseline_members")}) {
        if (!is_kind(members, Kind::array) || members->array.empty() || members->array.size() > 2)
            fail("invalid baseline state membership");
        bool has_save = false, has_room = false;
        for (const Value& member : members->array) {
            const Value* path = member.kind == Kind::object ? get(member, U"path") : nullptr;
            const bool is_save = is_string(path, save), is_room = is_string(path, room);
            if (member.kind != Kind::object || !is_string(get(member, U"type"), U"file") || (!is_save && !is_room)
                || (is_save && has_save) || (is_room && has_room)
                || !integer_between(get(member, U"size"), "0", "16777216")
                || !is_kind(get(member, U"sha256"), Kind::string) || !lower_sha256(get(member, U"sha256")->string))
                fail("invalid baseline member evidence");
            has_save = has_save || is_save;
            has_room = has_room || is_room;
        }
        if (!has_save) fail("session baseline requires a save");
    }
}

std::string encode(const Value& journal) {
    try { return compat::JournalEvidenceV1(journal); }
    catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
    catch (const compat::Error& e) { throw StoreError(e.what()); }
}

void persist(const fs::path& root, const Value& journal, const store_write::FailureHook& hook) {
    store_paths::safe_path(root);
    validate(journal, store_paths::native_points(root.filename()));
    const std::string content = encode(journal);
    if (content.size() > max_journal_bytes) fail("session journal exceeds limit; evidence retained for review");
    const fs::path target = root / "journal.json";
    store_paths::safe_path(target);
    store_write::atomic_write(target, content, hook);
}

Value read(const Store& store, std::u32string_view identity) {
    const fs::path path = store_write::session_root(store, identity) / "journal.json";
    store_paths::safe_path(path);
    Value journal;
    try {
        std::error_code error;
        const auto size = fs::file_size(path, error);
        if (error) throw fs::filesystem_error("stat", path, error);
        if (size > max_journal_bytes) fail("session journal exceeds limit");
        ReadPolicy policy;
        policy.max_bytes = max_journal_bytes;
        const auto bytes = store_paths::read(path, policy);
        if (!bytes) throw fs::filesystem_error("open", path, std::make_error_code(std::errc::no_such_file_or_directory));
        journal = compat::parse(*bytes);
    } catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what());
    } catch (const fs::filesystem_error& e) { throw StoreError(std::string("cannot read session journal: ") + e.what());
    } catch (const compat::Error& e) {
        // The reference raises the duplicate-key FrontendError unprefixed.
        const std::string what = e.what();
        if (what.rfind("duplicate JSON key", 0) == 0) throw StoreError(what);
        throw StoreError("cannot read session journal: " + what);
    }
    validate(journal, identity);
    return journal;
}

void transition(const fs::path& root, Value& journal, std::string_view state, const Fields& fields,
                const store_write::FailureHook& hook) {
    const std::u32string target(state.begin(), state.end());
    const Value* current = journal.kind == Kind::object ? get(journal, U"state") : nullptr;
    // The reference indexes TRANSITIONS[journal['state']] (KeyError/TypeError otherwise).
    if (!is_kind(current, Kind::string) || !known_state(current->string))
        throw std::invalid_argument("journal state is not a known session state");
    if (!transition_allowed(current->string, target))
        throw StoreError("illegal session transition: " + std::string(current->string.begin(), current->string.end())
                         + " -> " + std::string(state));
    Value updated = journal;
    auto assign = [&updated](std::u32string_view key, Value value) {
        for (auto& member : updated.object) if (member.first == key) { member.second = std::move(value); return; }
        updated.object.emplace_back(std::u32string(key), std::move(value));
    };
    for (const auto& field : fields) assign(field.first, field.second);
    assign(U"state", planning_internal::string_value(target));
    Value* events = nullptr;
    for (auto& member : updated.object) if (member.first == U"transitions") events = &member.second;
    if (!events || events->kind != Kind::array) throw std::invalid_argument("journal transitions must be a list");
    Value event = planning_internal::object_value();
    event.object.emplace_back(U"state", planning_internal::string_value(target));
    event.object.emplace_back(U"at", planning_internal::ascii_value(store_write::now()));
    events->array.push_back(std::move(event));
    persist(root, updated, hook);
    journal = std::move(updated);
}
}
