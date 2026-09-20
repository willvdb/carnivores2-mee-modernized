#include "c2/frontend/discovery.hpp"
#include "discovery_internal.hpp"
#include "content_internal.hpp"
#include "manifest_access.hpp"
#include "schema_compat.hpp"
#include "store_paths.hpp"
#include <algorithm>

namespace c2::frontend {
namespace fs = std::filesystem;
using compat::Value;
using compat::Kind;
namespace ci = content_internal;
namespace {
#ifdef _WIN32
const std::u32string flavor = U"nt";
#else
const std::u32string flavor = U"posix";
#endif
Value text(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
Value ascii(const std::string& s) { return text({s.begin(), s.end()}); }
Value boolean(bool b) { Value v; v.kind = Kind::boolean; v.boolean = b; return v; }
Value array() { Value v; v.kind = Kind::array; return v; }
Value object() { Value v; v.kind = Kind::object; return v; }
Value& field(Value& v, std::u32string_view key) {
    for (auto& p : v.object) if (p.first == key) return p.second;
    throw ContentError("missing observation field");
}
Value diagnostic(std::u32string code, std::u32string message) {
    auto v = object(); v.object = {{U"code", text(std::move(code))}, {U"message", text(std::move(message))}}; return v;
}
void append(Value& result, std::u32string code, std::u32string message) {
    field(result, U"diagnostics").array.push_back(diagnostic(std::move(code), std::move(message)));
}
bool directory(const fs::path& p) { return fs::is_directory(ci::source_status(p)); }
bool regular(const fs::path& p) { return fs::is_regular_file(ci::source_status(p)); }
std::u32string suffix(const std::u32string& name) {
    auto dot = name.find_last_of(U'.');
    return dot != name.npos && dot && dot + 1 < name.size() ? name.substr(dot) : U"";
}
// bytes regex: ASCII word boundary, re.I ASCII letters and ASCII whitespace.
// No Unicode decoding, locale character classes or script parser is involved.
bool script_block(std::string_view bytes, std::string_view key) {
    auto word = [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; };
    for (std::size_t i = 0; i + key.size() <= bytes.size(); ++i) {
        if (i && word(static_cast<unsigned char>(bytes[i - 1]))) continue;
        std::size_t j = 0;
        for (; j < key.size(); ++j) {
            auto c = static_cast<unsigned char>(bytes[i + j]);
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c != static_cast<unsigned char>(key[j])) break;
        }
        if (j != key.size()) continue;
        j += i;
        while (j < bytes.size() && (bytes[j] == ' ' || (bytes[j] >= '\t' && bytes[j] <= '\r'))) ++j;
        if (j < bytes.size() && bytes[j] == '{') return true;
    }
    return false;
}
Value recognize_value(const fs::path& source) {
    auto root = store_paths::resolve_root(ci::source_spelling(source));
    auto out = object(); auto caps = object();
    caps.object = {{U"content_recognized", text(U"no")}, {U"bundled_engine_evidence", text(U"unknown")}, {U"modern_engine_compatibility", text(U"unknown")}};
    out.object = {{U"path", text(store_paths::native_points(root))}, {U"recognized", boolean(false)},
        {U"diagnostics", array()}, {U"executables", array()}, {U"capabilities", std::move(caps)}};
    if (!directory(root)) {
        append(out, U"missing-installation", U"Registered root is unavailable."); return out;
    }
    for (const auto* reference : {U"HUNTDAT/_RES.TXT", U"HUNTDAT/MENU", U"HUNTDAT/AREAS"}) {
        auto status = resolve_reference(root, reference);
        if (status.status != ReferenceStatus::found) {
            auto d = diagnostic(U"incomplete-root", U"Required coherent-root evidence unavailable.");
            auto value = compat::parse(status.export_json());
            for (auto& p : value.object) d.object.push_back(std::move(p));
            field(out, U"diagnostics").array.push_back(std::move(d));
        }
    }
    std::vector<fs::path> children;
    for (const auto& e : fs::directory_iterator(ci::source_syscall(root))) children.push_back(e.path());
    std::stable_sort(children.begin(), children.end(), [](const fs::path& a, const fs::path& b) {
        auto x = store_paths::native_points(a.filename()), y = store_paths::native_points(b.filename());
#ifdef _WIN32
        x = schema::lower(x); y = schema::lower(y);
#endif
        return x < y;
    });
    for (const auto& p : children) {
        if (!regular(p) || ci::source_is_link(p)) continue;
        auto name = store_paths::native_points(p.filename());
        auto ext = schema::lower(suffix(name)), lower = schema::lower(name);
        if (ext == U".exe" || ext == U".ren" || lower == U"carnivores2" || lower == U"carnivores2-gl")
            field(out, U"executables").array.push_back(text(std::move(name)));
    }
    auto areas = resolve_reference(root, U"HUNTDAT/AREAS");
    std::vector<std::u32string> pairs;
    if (areas.status == ReferenceStatus::found) {
        auto dir = root / ci::native_units(*areas.path);
        if (directory(dir)) for (const auto& entry : fs::directory_iterator(ci::source_syscall(dir))) {
            auto p = entry.path(); auto name = store_paths::native_points(p.filename());
            auto ext = suffix(name);
            if (regular(p) && schema::lower(ext) == U".map") {
                auto stem = name.substr(0, name.size() - ext.size());
                auto rsc = resolve_reference(root, *areas.path + U"/" + stem + U".rsc");
                if (rsc.status == ReferenceStatus::found && regular(root / ci::native_units(*rsc.path)) &&
                    schema::casefold(stem).rfind(U"trophy", 0) != 0) pairs.push_back(std::move(name));
            }
        }
    }
    std::sort(pairs.begin(), pairs.end()); auto pair_values = array();
    for (auto& p : pairs) pair_values.array.push_back(text(std::move(p)));
    out.object.emplace_back(U"map_pairs", std::move(pair_values));
    if (pairs.empty()) append(out, U"missing-map-pair", U"No paired MAP/RSC content.");
    for (const auto* reference : {U"HUNTDAT/_RES.TXT", U"HUNTDAT/MENU"}) {
        auto status = resolve_reference(root, reference);
        if (status.status != ReferenceStatus::found) continue;
        auto p = root / ci::native_units(*status.path);
        bool dir = std::u32string_view(reference) == U"HUNTDAT/MENU";
        if (dir ? !directory(p) : !regular(p)) append(out, U"invalid-root-entry", reference);
        else if (!dir) {
            constexpr std::size_t limit = 8 * 1024 * 1024;
            auto bytes = ci::read_prefix(p, limit + 1);
            if (bytes.size() > limit || !script_block(bytes, "characters") || !script_block(bytes, "weapons"))
                append(out, U"missing-script-evidence", U"Resource script lacks conventional characters/weapons blocks.");
        }
    }
    bool recognized = field(out, U"diagnostics").array.empty();
    field(out, U"recognized") = boolean(recognized);
    field(field(out, U"capabilities"), U"content_recognized") = text(recognized ? U"yes" : U"no");
    bool engine = !field(out, U"executables").array.empty();
    field(field(out, U"capabilities"), U"bundled_engine_evidence") = text(engine ? U"candidate-files-only" : U"none");
    if (!engine) append(out, U"missing-engine-evidence", U"No bundled engine/launcher candidate; content recognition does not certify execution.");
    return out;
}
Value engines(const fs::path& root, const Value& observation) {
    auto out = array();
    for (const auto& name : observation.at(U"executables").array) {
        auto item = object(); item.object = {{U"path", name}, {U"sha256", ascii(ci::hash_file(root / ci::native_units(name.string)))}, {U"semantics", text(U"unknown")}};
        out.array.push_back(std::move(item));
    }
    return out;
}
Value revision_value(const fs::path& path) {
    auto f = fingerprint(path); auto v = object(); Value count; count.kind = Kind::integer;
    count.integer = std::to_string(f.file_count()); Value bytes; bytes.kind = Kind::integer; bytes.integer = f.byte_count();
    v.object = {{U"algorithm", ascii(f.algorithm())}, {U"sha256", ascii(f.sha256())}, {U"file_count", std::move(count)}, {U"byte_count", std::move(bytes)}};
    return v;
}
std::optional<bool> optional_bool(const Value& v, std::u32string_view key) {
    return v.contains(key) ? std::optional<bool>(v.at(key).boolean) : std::nullopt;
}
}
namespace discovery_internal {
Value inspect(const Value& instance) {
    Value out;
    if (instance.at(U"path_flavor").string != flavor) {
        out = object(); auto d = array(); d.array.push_back(diagnostic(U"foreign-path", U"Explicit relocation needed on this OS."));
        out.object = {{U"recognized", boolean(false)}, {U"diagnostics", std::move(d)}};
    } else out = recognize_value(ci::native_units(instance.at(U"path").string));
    if (out.at(U"recognized").boolean) {
        auto path = ci::native_units(instance.at(U"path").string);
        auto revision = revision_value(path); bool changed = !schema::equal(revision, instance.at(U"revision"));
        out.object.emplace_back(U"revision", std::move(revision)); out.object.emplace_back(U"revision_changed", boolean(changed));
        if (changed) append(out, U"content-revision-changed", U"Interpretations require review; saves are unchanged by inspection.");
        auto evidence = engines(path, out);
        bool engine_changed = !schema::equal(evidence, instance.contains(U"engine_evidence") ? instance.at(U"engine_evidence") : array());
        out.object.emplace_back(U"engine_evidence", std::move(evidence)); out.object.emplace_back(U"engine_changed", boolean(engine_changed));
        if (engine_changed) append(out, U"engine-evidence-changed", U"Engine/launcher bytes changed independently of content revision.");
    }
    bool review = (instance.contains(U"engine_relocation_reviews") && schema::truth(instance.at(U"engine_relocation_reviews"))) ||
        (out.contains(U"engine_changed") && schema::truth(out.at(U"engine_changed")));
    out.object.emplace_back(U"engine_review_required", boolean(review));
    if (review) append(out, U"engine-review-required", U"Engine evidence requires reviewed reconciliation; relocation and refresh do not accept a new engine baseline.");
    return out;
}
}
struct DiscoveryObservation::Impl { Value value; std::optional<std::vector<std::u32string>> executables; };
struct DiscoveryAccess {
    static DiscoveryObservation make(Value value) {
        auto p = std::make_shared<DiscoveryObservation::Impl>(); p->value = std::move(value);
        if (p->value.contains(U"executables")) {
            p->executables.emplace();
            for (const auto& n : p->value.at(U"executables").array) p->executables->push_back(n.string);
        }
        return DiscoveryObservation(std::move(p));
    }
};
DiscoveryObservation::DiscoveryObservation(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
bool DiscoveryObservation::recognized() const noexcept { return impl_->value.at(U"recognized").boolean; }
std::optional<std::u32string> DiscoveryObservation::path() const {
    return impl_->value.contains(U"path") ? std::optional<std::u32string>(impl_->value.at(U"path").string) : std::nullopt;
}
const std::optional<std::vector<std::u32string>>& DiscoveryObservation::executables() const noexcept { return impl_->executables; }
std::optional<bool> DiscoveryObservation::revision_changed() const { return optional_bool(impl_->value, U"revision_changed"); }
std::optional<bool> DiscoveryObservation::engine_changed() const { return optional_bool(impl_->value, U"engine_changed"); }
std::optional<bool> DiscoveryObservation::engine_review_required() const { return optional_bool(impl_->value, U"engine_review_required"); }
std::string DiscoveryObservation::export_json() const { return compat::display(impl_->value); }
DiscoveryObservation recognize(const fs::path& path) { return DiscoveryAccess::make(recognize_value(path)); }
std::vector<EngineEvidence> engine_evidence(const fs::path& root, const DiscoveryObservation& observation) {
    if (!observation.executables()) throw ContentError("executables are absent from observation");
    std::vector<EngineEvidence> result;
    for (const auto& name : *observation.executables()) result.push_back({name, ci::hash_file(root / ci::native_units(name)), U"unknown"});
    return result;
}
std::vector<DiscoveryObservation> discover(const fs::path& source) {
    auto root = ci::source_spelling(store_paths::expand_user(ci::source_spelling(source)));
    if (root.native().find(fs::path::value_type{}) != fs::path::string_type::npos) return {};
    struct Pending { fs::path path; std::vector<fs::path> ancestors; };
    std::vector<Pending> pending{{root, {}}}; std::vector<DiscoveryObservation> out;
    while (!pending.empty()) {
        auto item = std::move(pending.back()); pending.pop_back();
        std::error_code ec; fs::directory_iterator it(ci::source_syscall(item.path), ec), end;
        std::vector<fs::path> dirs;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code kind_error;
            if (fs::is_directory(it->path(), kind_error)) dirs.push_back(it->path().filename());
        }
        if (ec) continue; // os.walk omits failed/interrupted scandir.
        auto canonical = store_paths::resolve_native(item.path);
        for (const auto& a : item.ancestors) {
#ifdef _WIN32
            bool same = store_paths::windows_path_equal(store_paths::native_points(a), store_paths::native_points(canonical));
#else
            bool same = a == canonical;
#endif
            if (same) throw ContentError("discovery directory cycle");
        }
        item.ancestors.push_back(std::move(canonical));
        dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [&](const fs::path& p) { return ci::source_is_link(item.path / p); }), dirs.end());
        std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return store_paths::native_points(a) < store_paths::native_points(b); });
        auto huntdat = [](const fs::path& p) { return schema::casefold(store_paths::native_points(p)) == U"huntdat"; };
        if (std::any_of(dirs.begin(), dirs.end(), huntdat)) out.push_back(recognize(item.path));
        for (auto i = dirs.rbegin(); i != dirs.rend(); ++i) if (!huntdat(*i) && !ci::source_is_link(item.path / *i)) pending.push_back({item.path / *i, item.ancestors});
    }
    return out;
}
struct InstanceObservation::Impl { Value value; ReadPolicy policy; };
InstanceObservation::InstanceObservation(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
const std::u32string& InstanceObservation::id() const noexcept { return impl_->value.at(U"id").string; }
const std::u32string& InstanceObservation::path() const noexcept { return impl_->value.at(U"path").string; }
const std::u32string& InstanceObservation::path_flavor() const noexcept { return impl_->value.at(U"path_flavor").string; }
std::string InstanceObservation::export_json() const { return compat::display(impl_->value, impl_->policy.max_depth); }
InstanceObservation get_instance(const Manifest& manifest, const std::u32string& identity) {
    const auto& instances = ManifestAccess::instances(manifest);
    if (!instances.contains(identity)) throw StoreError("unknown instance ID");
    auto p = std::make_shared<InstanceObservation::Impl>(); p->value = instances.at(identity); p->policy = ManifestAccess::policy(manifest);
    return InstanceObservation(std::move(p));
}
DiscoveryObservation inspect_instance(const InstanceObservation& instance) { return DiscoveryAccess::make(discovery_internal::inspect(instance.impl_->value)); }
std::vector<std::u32string> move_candidates(const Manifest& manifest, const fs::path& path) {
    auto revision = revision_value(path); std::vector<std::u32string> out;
    for (const auto& entry : ManifestAccess::instances(manifest).object) {
        const auto& i = entry.second;
        if ((i.at(U"path_flavor").string != flavor || !fs::exists(ci::source_status(ci::native_units(i.at(U"path").string)))) && schema::equal(i.at(U"revision"), revision)) out.push_back(i.at(U"id").string);
    }
    return out;
}
}
