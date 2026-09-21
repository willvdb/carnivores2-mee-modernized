#include "manifest_schema.hpp"
#include "c2/frontend/core.hpp"
#include "schema_compat.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace c2::frontend::schema {
using compat::Kind;
using compat::Value;
using S = std::u32string;
bool valid_id(std::u32string_view s) {
    if (s.size() != 36)
        return false;
    for (std::size_t i = 0; i < 36; ++i) {
        auto c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != U'-')
                return false;
        } else if (!((c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'f')))
            return false;
    }
    return true;
}
namespace {
const Value missing;
const Value &get(const Value &v, std::u32string_view k) {
    return v.kind == Kind::object && v.contains(k) ? v.at(k) : missing;
}
bool obj(const Value &v) { return v.kind == Kind::object; }
bool arr(const Value &v) { return v.kind == Kind::array; }
bool str(const Value &v) { return v.kind == Kind::string; }
bool is(const Value &v, std::u32string_view s) { return str(v) && v.string == s; }
bool one(const Value &v, std::initializer_list<std::u32string_view> choices) {
    for (auto s : choices)
        if (is(v, s))
            return true;
    return false;
}
void need(bool b, const char *message) {
    if (!b)
        throw compat::Error(message);
}
bool integer(const Value &v, const char *n) { return v.kind == Kind::integer && v.integer == n; }
bool nonnegative(const Value &v) {
    return v.kind == Kind::integer && !v.integer.empty() && v.integer[0] != '-';
}
bool false_value(const Value &v) { return v.kind == Kind::boolean && !v.boolean; }
bool text(const Value &v) { return str(v) && !v.string.empty(); }
bool clean_text(const Value &v) { return text(v) && v.string.find(U'\0') == S::npos; }
bool digest(const Value &v) {
    return str(v) && v.string.size() == 64 &&
           std::all_of(v.string.begin(), v.string.end(), [](char32_t c) {
               return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'f');
           });
}
bool id(const Value &v) { return str(v) && valid_id(v.string); }
Value string_value(const S &s) {
    Value v;
    v.kind = Kind::string;
    v.string = s;
    return v;
}
S ascii(const std::string &s) { return S(s.begin(), s.end()); }
bool revision(const Value &v) {
    return obj(v) && is(get(v, U"algorithm"), U"huntdat-sha256-v1") && digest(get(v, U"sha256")) &&
           nonnegative(get(v, U"file_count")) && nonnegative(get(v, U"byte_count"));
}
PurePath locator(const Value &v) {
    need(obj(v) && one(get(v, U"path_flavor"), {U"posix", U"nt"}),
         "invalid path flavor or locator");
    need(clean_text(get(v, U"path")), "invalid installation locator");
    auto p = path(get(v, U"path").string, is(get(v, U"path_flavor"), U"nt"));
    need(p.absolute() && !p.parent(), "locator must be absolute without parent traversal");
    return p;
}
void engines(const Value &v) {
    need(arr(v), "invalid engine evidence");
    std::set<S> paths;
    for (const auto &e : v.array) {
        const auto &p = get(e, U"path");
        need(obj(e) && text(p) && p.string != U"." && p.string != U".." &&
                 p.string.find_first_of(S(U"/\\:\0", 4)) == S::npos &&
                 paths.insert(p.string).second && digest(get(e, U"sha256")) &&
                 is(get(e, U"semantics"), U"unknown"),
             "invalid engine evidence entry");
    }
}
bool safe_member(const Value &p, bool strict) {
    if (!text(p))
        return false;
#ifdef _WIN32
    constexpr bool native_nt = true;
#else
    constexpr bool native_nt = false;
#endif
    auto parsed = path(p.string, native_nt);
    return !parsed.absolute() && !parsed.parent() &&
           p.string.find_first_of(strict ? S(U"\\:\0", 3) : S(U"\\:")) == S::npos;
}
bool members(const Value &v) {
    if (!arr(v) || v.array.empty())
        return false;
    std::set<S> names;
    S previous;
    bool first = true;
    for (const auto &m : v.array) {
        auto &p = get(m, U"path");
        if (!obj(m) || !safe_member(p, true) || !names.insert(casefold(p.string)).second ||
            !is(get(m, U"type"), U"file") || !nonnegative(get(m, U"size")) ||
            !digest(get(m, U"sha256")) || (!first && p.string < previous))
            return false;
        previous = p.string;
        first = false;
    }
    return true;
}
Value projection(const Value &a) {
    Value v;
    v.kind = Kind::object;
    for (auto k : {U"id", U"hunter_id", U"instance_id", U"filename_slot", U"state_key", U"origin",
                   U"revision"}) {
        need(a.contains(k), "malformed managed-state metadata");
        v.object.emplace_back(k, a.at(k));
    }
    return v;
}
Value imported(const Value &a) {
    const auto &files = get(a, U"files");
    need(arr(files), "malformed managed-state metadata");
    Value v;
    v.kind = Kind::array;
    for (auto &f : files.array) {
        need(obj(f) && f.contains(U"path") && f.contains(U"size") && f.contains(U"sha256"),
             "malformed managed-state metadata");
        Value m;
        m.kind = Kind::object;
        for (auto k : {U"path", U"size", U"sha256"})
            m.object.emplace_back(k, f.at(k));
        m.object.emplace_back(U"type", string_value(U"file"));
        v.array.push_back(std::move(m));
    }
    std::sort(v.array.begin(), v.array.end(), [](const Value &a, const Value &b) {
        need(str(get(a, U"path")) && str(get(b, U"path")), "malformed managed-state metadata");
        return get(a, U"path").string < get(b, U"path").string;
    });
    return v;
}
bool execution(const Value &spec, const Value &slot) {
    static const Value capability = compat::parse(
        R"({"contract":"c2-engine-session","version":1,"state":"sav-sab-pair","layout":"state-config-output-v1","performance_capture":false})");
    static const S config_digest =
        ascii(sha256("display_mode 0\nresolution 800x600\nfps_limit 1\nglperf_logging 0\n"));
    const auto &e = get(spec, U"executable");
    const auto &argv = get(spec, U"argv");
    const auto &contract = get(spec, U"contract");
    const auto &timeout = get(spec, U"timeout_seconds");
    bool duration = false;
    if (timeout.kind == Kind::floating)
        duration = timeout.floating >= 30 && timeout.floating <= 3600;
    else if (nonnegative(timeout))
        duration = (timeout.integer.size() == 2 && timeout.integer >= "30") ||
                   (timeout.integer.size() == 3) ||
                   (timeout.integer.size() == 4 && timeout.integer <= "3600");
    if (!obj(spec) || !is(get(spec, U"kind"), U"managed-native-hunt-v1") || !obj(e) ||
        !clean_text(get(e, U"path")) || !digest(get(e, U"sha256")) ||
        !equal(contract, capability) || get(contract, U"version").kind != Kind::integer ||
        get(contract, U"performance_capture").kind != Kind::boolean ||
        !false_value(get(spec, U"shell")) || !duration ||
        !is(get(spec, U"config_sha256"), config_digest) || !clean_text(get(spec, U"cwd")) ||
        !arr(argv) || argv.array.size() != 11)
        return false;
    for (const auto &arg : argv.array)
        if (!clean_text(arg))
            return false;
    if (!equal(argv.array[0], get(e, U"path")) || !is(argv.array[1], U"--session-contract=1") ||
        !is(argv.array[2], U"--session-slot=" + ascii(slot.integer)))
        return false;
    std::size_t i = 3;
    for (auto prefix : {U"--session-root=", U"--session-source=", U"--session-baseline="}) {
        const S p = prefix;
        const auto &s = argv.array[i++].string;
        if (s.size() <= p.size() || s.compare(0, p.size(), p) != 0)
            return false;
    }
    const auto &area = argv.array[6].string;
    if (area != U"prj=huntdat/areas/external" &&
        !(area.size() == 23 && area.substr(0, 22) == U"prj=huntdat/areas/area" &&
          area[22] >= U'1' && area[22] <= U'8'))
        return false;
    bool din = false, wep = false;
    for (int bit = 0; bit < 9; ++bit)
        if (is(argv.array[7], U"din=" + ascii(std::to_string(1 << bit))))
            din = true;
    for (int bit = 0; bit < 8; ++bit)
        if (is(argv.array[8], U"wep=" + ascii(std::to_string(1 << bit))))
            wep = true;
    return din && wep && one(argv.array[9], {U"dtm=0", U"dtm=1", U"dtm=2"}) &&
           is(argv.array[10], U"smod=0.85,0.70,0.80,1.0,1.25,1.0");
}
std::set<S> history(const Value &a) {
    const auto &h = get(a, U"managed_state");
    const auto &gs = get(h, U"generations");
    const auto &rs = get(h, U"receipts");
    const auto &head = get(h, U"current_generation");
    need(obj(h) && integer(get(h, U"schema_version"), "1") && obj(gs) && obj(rs) && id(head) &&
             gs.contains(head.string),
         "invalid managed-state history/head");
    const Value *current = &head;
    std::set<S> seen;
    std::vector<const Value *> chain;
    const auto prov = projection(a);
    while (current->kind != Kind::null) {
        need(id(*current) && seen.insert(current->string).second && gs.contains(current->string),
             "invalid generation ancestry");
        const auto &g = gs.at(current->string);
        bool required = obj(g);
        for (auto k : {U"id", U"sequence", U"predecessor", U"source_session", U"kind",
                       U"created_at", U"members", U"provenance", U"snapshot"})
            required = required && g.contains(k);
        need(required && equal(get(g, U"id"), *current) && members(get(g, U"members")) &&
                 equal(get(g, U"provenance"), prov) && text(get(g, U"created_at")) &&
                 get(g, U"sequence").kind == Kind::integer,
             "invalid generation record/provenance");
        chain.push_back(&g);
        current = &get(g, U"predecessor");
    }
    need(seen.size() == gs.object.size(), "unconnected generation metadata; no guessed head");
    std::reverse(chain.begin(), chain.end());
    std::set<S> sessions;
    for (std::size_t sequence = 0; sequence < chain.size(); ++sequence) {
        const auto &g = *chain[sequence];
        need(integer(get(g, U"sequence"), std::to_string(sequence).c_str()),
             "noncontiguous generation history");
        if (sequence == 0) {
            need(is(get(g, U"kind"), U"original-import") &&
                     get(g, U"source_session").kind == Kind::null &&
                     is(get(g, U"snapshot"), U"snapshots/" + get(a, U"id").string) &&
                     equal(get(g, U"members"), imported(a)),
                 "generation zero differs from original import provenance");
            continue;
        }
        const auto &sid = get(g, U"source_session");
        const auto &r = str(sid) ? get(rs, sid.string) : missing;
        bool ok = id(sid) && sessions.insert(sid.string).second &&
                  is(get(g, U"kind"), U"accepted-native-hunt") &&
                  is(get(g, U"snapshot"),
                     U"generations/" + get(a, U"id").string + U"/" + get(g, U"id").string) &&
                  obj(r) && integer(get(r, U"schema_version"), "1") &&
                  equal(get(r, U"association_id"), get(a, U"id")) &&
                  equal(get(r, U"session_id"), sid) &&
                  equal(get(r, U"generation_id"), get(g, U"id"));
        for (auto k : {U"predecessor", U"members"})
            ok = ok && equal(get(r, k), get(g, k));
        ok = ok && equal(get(r, U"accepted_at"), get(g, U"created_at")) &&
             is(get(r, U"policy"), U"genesis-current-mee-hunt-v1") &&
             equal(get(g, U"policy"), get(r, U"policy")) &&
             execution(get(g, U"execution"), get(a, U"filename_slot")) &&
             equal(get(g, U"execution"), get(r, U"execution")) &&
             equal(get(g, U"revision"), get(a, U"revision")) &&
             equal(get(r, U"revision"), get(a, U"revision")) &&
             digest(get(r, U"candidate_sha256")) && is(get(r, U"acceptance"), U"explicit");
        need(ok, "invalid authoritative acceptance receipt");
    }
    need(sessions.size() == rs.object.size(), "orphan acceptance receipt");
    return sessions;
}
} // namespace
void validate_manifest(const Value &d) {
    need(obj(d) &&
             (integer(get(d, U"schema_version"), "1") || integer(get(d, U"schema_version"), "2")),
         "unsupported manifest schema; explicit migration required");
    for (auto t : {U"hunters", U"instances", U"associations", U"host_settings"})
        need(obj(get(d, t)), "invalid manifest table object");
    for (auto t : {U"hunters", U"instances", U"associations"})
        for (auto &[k, v] : get(d, t).object)
            need(id(string_value(k)) && obj(v) && is(get(v, U"id"), k),
                 "invalid identity in manifest table");
    const auto &hunters = get(d, U"hunters");
    const auto &instances = get(d, U"instances");
    for (auto &p : hunters.object)
        need(str(get(p.second, U"name")) && !blank(get(p.second, U"name").string),
             "hunter name is required");
    const auto &active = get(d, U"active_hunter");
    need(active.kind == Kind::null || (str(active) && hunters.contains(active.string) &&
                                       !truth(get(hunters.at(active.string), U"archived_at"))),
         "active hunter is missing or archived");
    std::set<std::pair<S, S>> paths;
    for (auto &p : instances.object) {
        const auto &i = p.second;
        need(one(get(i, U"mode"), {U"managed", U"registered"}), "invalid installation mode");
        auto loc = locator(i);
        const auto &managed = get(i, U"managed_root");
        if (managed.kind != Kind::null) {
            auto root = locator(managed);
            need(!is(get(i, U"mode"), U"managed") || loc.below(root),
                 "managed installation must be below its recorded managed root");
        }
        need(one(get(i, U"dialect_hint"),
                 {U"unknown", U"c2-classic", U"iceage-triassic", U"mee-older", U"mee-newer"}),
             "invalid dialect hint");
        need(paths.emplace(get(i, U"path_flavor").string, get(i, U"path").string).second,
             "duplicate installation locator");
        const auto &revisions = get(i, U"revisions");
        need(arr(revisions) && !revisions.array.empty(), "installation revision history required");
        need(std::any_of(revisions.array.begin(), revisions.array.end(),
                         [&](const Value &v) { return equal(get(i, U"revision"), v); }),
             "current revision absent from history");
        for (auto &r : revisions.array)
            need(revision(r), "invalid content revision");
        engines(get(i, U"engine_evidence"));
        if (i.contains(U"engine_relocation_reviews")) {
            const auto &reviews = i.at(U"engine_relocation_reviews");
            need(arr(reviews), "invalid engine relocation review history");
            for (auto &r : reviews.array) {
                need(obj(r) && is(get(r, U"status"), U"required") && text(get(r, U"observed_at")),
                     "invalid engine relocation review");
                locator(get(r, U"from"));
                locator(get(r, U"to"));
                engines(get(r, U"baseline_engine_evidence"));
                engines(get(r, U"destination_engine_evidence"));
                need(equal(get(r, U"baseline_engine_evidence"), get(i, U"engine_evidence")),
                     "engine relocation review must retain registration baseline");
            }
        }
    }
    const bool v2 = integer(get(d, U"schema_version"), "2");
    if (v2) {
        const auto &u = get(d, U"state_upgrade");
        need(obj(u) && integer(get(u, U"from_version"), "1") &&
                 is(get(u, U"backup"), U"lodge.schema-1.backup.json") && str(get(u, U"at")) &&
                 digest(get(u, U"backup_sha256")),
             "invalid manifest upgrade provenance");
    }
    std::set<std::pair<S, S>> referenced;
    std::set<S> accepted;
    for (auto &p : get(d, U"associations").object) {
        const auto &a = p.second;
        need(str(get(a, U"hunter_id")) && str(get(a, U"instance_id")),
             "invalid association reference");
        need(hunters.contains(get(a, U"hunter_id").string) &&
                 instances.contains(get(a, U"instance_id").string),
             "dangling state association");
        need(one(get(a, U"ownership"), {U"referenced", U"managed"}), "invalid state ownership");
        need(str(get(a, U"state_key")), "invalid native state key");
        need(nonnegative(get(a, U"filename_slot")), "invalid filename slot");
        need(one(get(a, U"origin"), {U"personal", U"bundled-example", U"unknown"}),
             "invalid origin declaration");
        need(false_value(get(a, U"writable")), "schema 1 does not authorize native-state writers");
        const bool ref = is(get(a, U"ownership"), U"referenced");
        S authority = ref ? U"native-files" : U"independent-snapshot";
        if (v2 && !ref) {
            authority = U"managed-state-history";
            for (auto &sid : history(a))
                need(accepted.insert(sid).second,
                     "session receipt belongs to multiple associations");
        }
        need(is(get(a, U"authority"), authority) && revision(get(a, U"revision")),
             "invalid association authority or revision");
        const auto &files = get(a, U"files");
        need(arr(files) && !files.array.empty(), "state files required");
        std::set<S> names;
        for (auto &f : files.array) {
            need(safe_member(get(f, U"path"), false), "unsafe state member path");
            need(digest(get(f, U"sha256")), "invalid state digest");
            need(one(get(f, U"kind"), {U"sav", U"sab"}) && nonnegative(get(f, U"size")),
                 "invalid native state member");
            need(names.insert(casefold(get(f, U"path").string)).second,
                 "ambiguous association member paths");
        }
        if (ref)
            need(referenced.emplace(get(a, U"instance_id").string, get(a, U"state_key").string)
                     .second,
                 "native state already associated; use an explicit independent copy");
    }
}
Value decode_manifest(std::string_view bytes) {
    auto v = compat::parse(bytes);
    validate_manifest(v);
    return v;
}
} // namespace c2::frontend::schema
