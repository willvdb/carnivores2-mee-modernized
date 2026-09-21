// Test-only line-framed filesystem adapter; never added to the production CLI.
#include "c2/frontend/catalog.hpp"
#include "content_internal.hpp"
#include "json_compat.hpp"
#include "store_paths.hpp"
#include <iostream>
#include <optional>
#include <type_traits>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
using compat::Value;
using compat::Kind;
static_assert(!std::is_default_constructible_v<catalog::Entry>);
static_assert(!std::is_default_constructible_v<catalog::Projection>);
static Value text(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
static Value ascii(std::string_view s) { return text({s.begin(), s.end()}); }
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(std::string("projection typed assertion: ") + what); }
static std::string decimal(std::size_t n) { return std::to_string(n); }
static bool same_integer(const std::optional<catalog::Integer>& typed, const Value& v, std::u32string_view key) {
    if (!v.contains(key) || v.at(key).kind != Kind::integer) return !typed;
    return typed && typed->decimal == v.at(key).integer;
}
static bool same_text(const std::optional<std::u32string>& typed, const Value& v, std::u32string_view key) {
    if (!v.contains(key) || v.at(key).kind != Kind::string) return !typed;
    return typed && *typed == v.at(key).string;
}
static bool same_strings(const std::optional<std::vector<std::u32string>>& typed, const Value& v, std::u32string_view key) {
    if (!v.contains(key)) return !typed;
    if (!typed || typed->size() != v.at(key).array.size()) return false;
    for (std::size_t i = 0; i < typed->size(); ++i) if ((*typed)[i] != v.at(key).array[i].string) return false;
    return true;
}
static bool same_attribute(const catalog::Attribute& a, const Value& v) {
    return a.key == v.at(U"key").string && a.raw == v.at(U"raw").string && decimal(a.line) == v.at(U"line").integer && a.source == v.at(U"source").string;
}
static const char* status_name(ReferenceStatus s) {
    switch (s) { case ReferenceStatus::found: return "found"; case ReferenceStatus::missing: return "missing";
        case ReferenceStatus::ambiguous: return "ambiguous"; case ReferenceStatus::unsafe: return "unsafe"; }
    return "?";
}
static bool same_reference(const ReferenceObservation& r, const Value& v) {
    return r.reference == v.at(U"reference").string && ascii(status_name(r.status)).string == v.at(U"status").string &&
        same_text(r.path, v, U"path") && r.export_json() == compat::display(compat::parse(r.export_json()));
}
static void check_text(const catalog::TextObservation& t, const Value& v) {
    const char* status = t.status == catalog::TextStatus::found ? "found" : t.status == catalog::TextStatus::missing ? "missing" :
        t.status == catalog::TextStatus::ambiguous ? "ambiguous" : t.status == catalog::TextStatus::unsafe ? "unsafe" : "too-large";
    require(t.reference == v.at(U"reference").string && ascii(status).string == v.at(U"status").string, "text status");
    require(same_text(t.path, v, U"path") && same_text(t.encoding, v, U"encoding") && same_strings(t.lines, v, U"lines"), "text fields");
    require(t.sha256.has_value() == v.contains(U"sha256") && (!t.sha256 || ascii(*t.sha256).string == v.at(U"sha256").string), "text digest");
    require(t.export_json() == compat::display(v), "text export");
}
static void check_entry(const catalog::Entry& e, const Value& v) {
    require(e.id() == v.at(U"id").string && e.kind() == v.at(U"kind").string && decimal(e.ordinal()) == v.at(U"ordinal").integer, "entry identity");
    require(e.source() == v.at(U"source").string && decimal(e.line()) == v.at(U"line").integer, "entry source");
    auto label = e.label(); const auto& stored = v.at(U"label");
    if (stored.kind == Kind::null) require(!label, "null label");
    else if (stored.kind == Kind::integer) require(label && std::get_if<catalog::Integer>(&*label) && std::get<catalog::Integer>(*label).decimal == stored.integer, "integer label");
    else require(label && std::get_if<std::u32string>(&*label) && std::get<std::u32string>(*label) == stored.string, "string label");
    require(same_integer(e.price(), v, U"price") && same_integer(e.ai(), v, U"ai"), "entry numbers");
    auto source = e.price_source();
    require(source.has_value() == v.contains(U"price_source") && (!source || same_attribute(*source, v.at(U"price_source"))), "price source presence");
    auto slot = e.slot();
    require(slot.has_value() == v.contains(U"slot") && (!slot || decimal(*slot) == v.at(U"slot").integer), "slot");
    require(same_text(e.launch_stem(), v, U"launch_stem"), "launch stem");
    auto references = e.references(); require(references.size() == v.at(U"references").array.size(), "reference count");
    for (std::size_t i = 0; i < references.size(); ++i) check_text(references[i], v.at(U"references").array[i]);
    auto declared = e.declared_references();
    require(declared.size() == (v.contains(U"declared_references") ? v.at(U"declared_references").array.size() : 0), "declared count");
    for (std::size_t i = 0; i < declared.size(); ++i) {
        const auto& d = v.at(U"declared_references").array[i];
        require(declared[i].field == d.at(U"field").string && same_reference(declared[i].observation, d), "declared reference");
    }
    auto candidates = e.map_candidates();
    require(candidates.size() == (v.contains(U"map_candidates") ? v.at(U"map_candidates").array.size() : 0), "candidate count");
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = v.at(U"map_candidates").array[i];
        require(candidates[i].stem == c.at(U"stem").string && same_reference(candidates[i].map, c.at(U"map")) && same_reference(candidates[i].rsc, c.at(U"rsc")), "map candidate");
    }
    require(e.export_json() == compat::display(v), "entry export");
}
static void check_projection(const catalog::Projection& p, const Value& v) {
    require(p.projection_version() == 1 && v.at(U"projection_version").integer == "1", "version");
    require(p.source() == v.at(U"source").string && ascii(p.source_sha256()).string == v.at(U"source_sha256").string, "source");
    auto d = p.dialect(); const auto& dv = v.at(U"dialect");
    require(d.hint == dv.at(U"hint").string && d.observed == dv.at(U"observed").string && d.effective == dv.at(U"effective").string && d.engine_build == dv.at(U"engine_build").string, "dialect");
    for (auto [group, key] : {std::pair{catalog::Group::areas, U"areas"}, std::pair{catalog::Group::licenses, U"licenses"},
                              std::pair{catalog::Group::weapons, U"weapons"}, std::pair{catalog::Group::equipment, U"equipment"}}) {
        auto entries = p.entries(group); require(entries.size() == v.at(key).array.size(), "entry count");
        for (std::size_t i = 0; i < entries.size(); ++i) check_entry(entries[i], v.at(key).array[i]);
    }
    auto starting = p.starting_score_observations(); require(starting.size() == v.at(U"starting_score_observations").array.size(), "starting count");
    for (std::size_t i = 0; i < starting.size(); ++i) require(same_attribute(starting[i], v.at(U"starting_score_observations").array[i]), "starting observation");
    auto modifiers = p.score_modifier_observations(); require(modifiers.size() == v.at(U"score_modifier_observations").array.size(), "modifier count");
    for (std::size_t i = 0; i < modifiers.size(); ++i) require(modifiers[i].export_json() == compat::display(v.at(U"score_modifier_observations").array[i]), "modifier node");
    require(same_strings(p.physical_maps(), v, U"physical_maps") && same_strings(p.presentation_references(), v, U"presentation_references") && same_strings(p.title_hints(), v, U"title_hints"), "lists");
    require(v.at(U"title").kind == Kind::null, "title");
    auto scripts = p.scripts(); const auto& sv = v.at(U"scripts");
    require(scripts.size() == sv.object.size(), "script count");
    for (std::size_t i = 0; i < scripts.size(); ++i) {
        require(scripts[i].first == sv.object[i].first && scripts[i].second.export_json() == compat::display(sv.object[i].second), "script export");
        require(scripts[i].second.source() == sv.object[i].second.at(U"source").string && ascii(scripts[i].second.sha256()).string == sv.object[i].second.at(U"sha256").string, "script identity");
    }
    auto diagnostics = p.diagnostics(); require(diagnostics.size() == v.at(U"diagnostics").array.size(), "diagnostic count");
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        const auto& a = diagnostics[i]; const auto& b = v.at(U"diagnostics").array[i];
        require(a.code == b.at(U"code").string && a.message == b.at(U"message").string, "diagnostic text");
        require(same_text(a.source, b, U"source") && same_text(a.entry_id, b, U"entry_id") && same_text(a.category, b, U"category"), "diagnostic strings");
        require(a.line.has_value() == b.contains(U"line") && (!a.line || decimal(*a.line) == b.at(U"line").integer), "diagnostic line");
        require(a.ordinal.has_value() == b.contains(U"ordinal") && (!a.ordinal || decimal(*a.ordinal) == b.at(U"ordinal").integer), "diagnostic ordinal");
        require(same_integer(a.ai, b, U"ai") && same_strings(a.entries, b, U"entries") && same_strings(a.paths, b, U"paths"), "diagnostic context");
        require(a.observation.has_value() == b.contains(U"observation") && (!a.observation || same_attribute(*a.observation, b.at(U"observation"))), "diagnostic observation");
        std::size_t typed = 2;
        for (auto key : {U"source", U"line", U"entry_id", U"category", U"ordinal", U"ai", U"entries", U"paths", U"observation"}) typed += b.contains(key);
        require(b.object.size() == typed, "diagnostic keys are all typed");
    }
    auto capabilities = p.capabilities(); const auto& cv = v.at(U"capabilities");
    require(capabilities.size() == cv.object.size(), "capability count");
    for (std::size_t i = 0; i < capabilities.size(); ++i) require(capabilities[i].first == cv.object[i].first && capabilities[i].second == cv.object[i].second.string, "capability");
}
static std::string run(const Value& r) {
    const auto& op = r.at(U"op").string;
    auto root = content_internal::native_units(r.at(U"root").string);
    if (op == U"text_reference") {
        auto observation = catalog::text_reference(root, r.at(U"reference").string);
        auto exported = observation.export_json();
        check_text(observation, compat::parse(exported));
        return exported;
    }
    if (op != U"project") throw std::runtime_error("unknown test operation");
    auto hint = r.contains(U"dialect_hint") ? r.at(U"dialect_hint").string : U"unknown";
    auto spelling = root; auto hint_copy = hint;
    std::optional<catalog::Projection> owner{catalog::project(spelling, hint_copy)};
    spelling.clear(); hint_copy.assign(U"changed"); // Caller buffers are independent.
    std::optional<catalog::Projection> copy{*owner};
    auto exported = copy->export_json();
    require(owner->export_json() == exported, "copy export");
    auto value = compat::parse(exported);
    check_projection(*copy, value);
    // Retain entries, scripts, nodes and diagnostics, release every projection, then query.
    std::vector<catalog::Entry> retained;
    for (auto group : {catalog::Group::areas, catalog::Group::licenses, catalog::Group::weapons, catalog::Group::equipment})
        for (auto& e : copy->entries(group)) retained.push_back(e);
    auto scripts = copy->scripts(); auto nodes = copy->score_modifier_observations(); auto diagnostics = copy->diagnostics();
    std::vector<std::string> before;
    for (const auto& e : retained) before.push_back(e.export_json());
    owner.reset(); copy.reset();
    for (std::size_t i = 0; i < retained.size(); ++i) require(retained[i].export_json() == before[i] && !retained[i].id().empty(), "retained entry");
    std::size_t index = 0;
    for (auto [group, key] : {std::pair{catalog::Group::areas, U"areas"}, std::pair{catalog::Group::licenses, U"licenses"},
                              std::pair{catalog::Group::weapons, U"weapons"}, std::pair{catalog::Group::equipment, U"equipment"}})
        for (const auto& e : value.at(key).array) check_entry(retained[index++], e);
    for (std::size_t i = 0; i < scripts.size(); ++i) require(scripts[i].second.export_json() == compat::display(value.at(U"scripts").object[i].second), "retained script");
    for (std::size_t i = 0; i < nodes.size(); ++i) require(nodes[i].export_json() == compat::display(value.at(U"score_modifier_observations").array[i]), "retained node");
    require(diagnostics.size() == value.at(U"diagnostics").array.size(), "retained diagnostics");
    return exported;
}
int main() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string line;
    while (std::getline(std::cin, line)) {
        Value out; out.kind = Kind::object; Value ok; ok.kind = Kind::boolean;
        auto failure = [&](std::string_view kind, const std::exception& e) {
            out.object = {{U"ok", ok}, {U"error", text(store_paths::native_points(std::filesystem::path(e.what())))}, {U"kind", ascii(kind)}};
        };
        try {
            auto bytes = run(compat::parse(line)); ok.boolean = true;
            out.object = {{U"ok", ok}, {U"value", compat::parse(bytes)}, {U"json", ascii(bytes)}};
        } catch (const catalog::ScalarConversionError& e) { failure("scalar-conversion", e); }
        catch (const catalog::Error& e) { failure("catalog", e); }
        catch (const std::exception& e) { failure("unexpected", e); }
        std::cout << compat::compact(out) << '\n' << std::flush;
    }
}
