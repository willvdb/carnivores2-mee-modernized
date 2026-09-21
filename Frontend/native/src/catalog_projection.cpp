#include "c2/frontend/catalog.hpp"
#include "catalog_internal.hpp"
#include "content_internal.hpp"
#include "c2/frontend/core.hpp"
#include "schema_compat.hpp"
#include "store_paths.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace c2::frontend::catalog {
namespace fs = std::filesystem;
using compat::Value;
using compat::Kind;
namespace ci = content_internal;
namespace {
Value string(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
Value ascii(std::string_view s) { return string({s.begin(), s.end()}); }
Value integer(std::string decimal) { Value v; v.kind = Kind::integer; v.integer = std::move(decimal); return v; }
Value number(std::size_t n) { return integer(std::to_string(n)); }
Value array() { Value v; v.kind = Kind::array; return v; }
Value object() { Value v; v.kind = Kind::object; return v; }
std::u32string digits(std::size_t n) { auto s = std::to_string(n); return {s.begin(), s.end()}; }
std::u32string latin1(std::string_view s) {
    std::u32string out; out.reserve(s.size());
    for (unsigned char c : s) out.push_back(c);
    return out;
}
Value& field(Value& v, std::u32string_view key) {
    for (auto& kv : v.object) if (kv.first == key) return kv.second;
    throw Error("missing internal projection field");
}
std::size_t index_of(const Value& v) { return static_cast<std::size_t>(std::stoull(v.integer)); }
std::uintmax_t size_of(const fs::path& p) {
    std::error_code ec; auto n = fs::file_size(ci::source_syscall(p), ec);
    if (ec) throw fs::filesystem_error("cannot stat catalog source", p, ec);
    return n;
}
bool regular(const fs::path& p) { return fs::is_regular_file(ci::source_status(p)); }
const char* status_name(ReferenceStatus s) {
    switch (s) { case ReferenceStatus::found: return "found"; case ReferenceStatus::missing: return "missing";
        case ReferenceStatus::ambiguous: return "ambiguous"; case ReferenceStatus::unsafe: return "unsafe"; }
    throw Error("invalid reference status");
}
const char* status_name(TextStatus s) {
    switch (s) { case TextStatus::found: return "found"; case TextStatus::missing: return "missing";
        case TextStatus::ambiguous: return "ambiguous"; case TextStatus::unsafe: return "unsafe"; case TextStatus::too_large: return "too-large"; }
    throw Error("invalid text status");
}
TextStatus text_status(ReferenceStatus s) {
    switch (s) { case ReferenceStatus::found: return TextStatus::found; case ReferenceStatus::missing: return TextStatus::missing;
        case ReferenceStatus::ambiguous: return TextStatus::ambiguous; case ReferenceStatus::unsafe: return TextStatus::unsafe; }
    throw Error("invalid reference status");
}
Value reference_value(const ReferenceObservation& r) {
    auto out = object(); out.object = {{U"reference", string(r.reference)}, {U"status", ascii(status_name(r.status))}};
    if (r.path) out.object.emplace_back(U"path", string(*r.path));
    return out;
}
Value text_value(const TextObservation& t) {
    auto out = object(); out.object = {{U"reference", string(t.reference)}, {U"status", ascii(status_name(t.status))}};
    if (t.path) out.object.emplace_back(U"path", string(*t.path));
    if (t.sha256) out.object.emplace_back(U"sha256", ascii(*t.sha256));
    if (t.encoding) out.object.emplace_back(U"encoding", string(*t.encoding));
    if (t.lines) {
        auto lines = array();
        for (const auto& line : *t.lines) lines.array.push_back(string(line));
        out.object.emplace_back(U"lines", std::move(lines));
    }
    return out;
}
Value scalar_value(const std::optional<Scalar>& s) {
    if (!s) return Value{};
    if (auto text = std::get_if<std::u32string>(&*s)) return string(*text);
    return integer(std::get<Integer>(*s).decimal);
}
Value diagnostic(std::u32string code, std::u32string message) {
    auto v = object(); v.object = {{U"code", string(std::move(code))}, {U"message", string(std::move(message))}}; return v;
}
// Python str.splitlines over the Latin-1 projection: LF, VT, FF, CR, CRLF,
// FS, GS, RS and NEL separate lines; no trailing empty element is produced.
std::vector<std::u32string> splitlines(std::string_view raw) {
    std::vector<std::u32string> out;
    std::size_t start = 0, i = 0;
    while (i < raw.size()) {
        auto c = static_cast<unsigned char>(raw[i]);
        if ((c >= 0x0a && c <= 0x0d) || (c >= 0x1c && c <= 0x1e) || c == 0x85) {
            out.push_back(latin1(raw.substr(start, i - start)));
            i += c == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n' ? 2 : 1;
            start = i;
        } else ++i;
    }
    if (start < raw.size()) out.push_back(latin1(raw.substr(start)));
    return out;
}
// Unicode \w restricted to the Latin-1 projection the script parser produces.
bool word(char32_t c) {
    if (c < 0x80) return (c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') || c == U'_';
    if (c > 0xff) throw Error("label outside Latin-1 projection");
    return c == 0xaa || c == 0xb2 || c == 0xb3 || c == 0xb5 || c == 0xb9 || c == 0xba || (c >= 0xbc && c <= 0xbe) ||
        (c >= 0xc0 && c != 0xd7 && c != 0xf7);
}
// re.search(r'\b(uncheck|check|select|click)\b', text, re.I): within Latin-1,
// case-insensitive equivalents of the ASCII pattern letters are ASCII only.
bool instruction_like(std::u32string_view text) {
    static const std::u32string_view words[] = {U"uncheck", U"check", U"select", U"click"};
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i && word(text[i - 1])) continue;
        for (auto w : words) {
            if (i + w.size() > text.size()) continue;
            std::size_t j = 0;
            for (; j < w.size(); ++j) {
                auto c = text[i + j];
                if (c >= U'A' && c <= U'Z') c += 32;
                if (c != w[j]) break;
            }
            if (j == w.size() && (i + j == text.size() || !word(text[i + j]))) return true;
        }
    }
    return false;
}
// Canonical non-negative decimals from scalar conversion: no sign, no leading zeros.
bool at_least_ten(const std::string& decimal) { return decimal[0] != '-' && decimal.size() >= 2; }
bool numeric_less(const std::string& a, const std::string& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; }
std::string minus_nine(std::string decimal) {
    unsigned borrow = 9;
    for (auto i = decimal.size(); i-- && borrow;) {
        unsigned d = static_cast<unsigned>(decimal[i] - '0');
        if (d >= borrow) { decimal[i] = static_cast<char>('0' + d - borrow); borrow = 0; }
        else { decimal[i] = static_cast<char>('0' + d + 10 - borrow); borrow = 1; }
    }
    auto nonzero = decimal.find_first_not_of('0');
    return nonzero == decimal.npos ? "0" : decimal.substr(nonzero);
}
std::u32string suffix(const std::u32string& name) {
    auto dot = name.find_last_of(U'.');
    return dot != name.npos && dot && dot + 1 < name.size() ? schema::lower(name.substr(dot)) : U"";
}
#ifdef _WIN32
constexpr bool nt = true;
#else
constexpr bool nt = false;
#endif
// PurePath.relative_to(root).as_posix(): purely lexical over the spelled parts.
std::u32string relative_posix(const schema::PurePath& root, const fs::path& p) {
    auto parsed = schema::path(store_paths::native_points(p), nt);
    if (parsed.drive != root.drive || parsed.root != root.root || parsed.parts.size() < root.parts.size() ||
        !std::equal(root.parts.begin(), root.parts.end(), parsed.parts.begin())) throw Error("walked path is not below the root");
    std::u32string out;
    for (auto i = root.parts.size(); i < parsed.parts.size(); ++i) {
        if (!out.empty()) out.push_back(U'/');
        out += parsed.parts[i];
    }
    return out;
}
std::vector<const Value*> blocks_of(const Value& script, std::u32string_view name) {
    std::vector<const Value*> out;
    for (const auto& node : script.at(U"tree").at(U"children").array) if (schema::casefold(node.at(U"name").string) == name) out.push_back(&node);
    return out;
}
std::optional<Scalar> attribute_of(const std::shared_ptr<const Value>& owner, const Value& node, std::u32string_view key) {
    return attribute(Access::node(std::shared_ptr<const Value>(owner, &node)), key);
}
const std::u32string OLDER[] = {U"hunterinfo", U"oldambients", U"corpseambients", U"mapambients"};
const std::u32string NEWER[] = {U"spawntable", U"packtable", U"trophytable"};
std::optional<Integer> integer_field(const Value& v, std::u32string_view key) {
    if (!v.contains(key) || v.at(key).kind != Kind::integer) return {};
    return Integer{v.at(key).integer};
}
Attribute attribute_from(const Value& v) { return {v.at(U"key").string, v.at(U"raw").string, index_of(v.at(U"line")), v.at(U"source").string}; }
ReferenceObservation reference_from(const Value& v) {
    ReferenceObservation out{v.at(U"reference").string, ReferenceStatus::unsafe, {}};
    const auto& status = v.at(U"status").string;
    out.status = status == U"found" ? ReferenceStatus::found : status == U"missing" ? ReferenceStatus::missing :
        status == U"ambiguous" ? ReferenceStatus::ambiguous : ReferenceStatus::unsafe;
    if (v.contains(U"path")) out.path = v.at(U"path").string;
    return out;
}
TextObservation text_from(const Value& v) {
    TextObservation out;
    auto reference = reference_from(v);
    out.reference = std::move(reference.reference); out.path = std::move(reference.path);
    out.status = v.at(U"status").string == U"too-large" ? TextStatus::too_large : text_status(reference.status);
    if (v.contains(U"sha256")) { const auto& s = v.at(U"sha256").string; out.sha256 = std::string(s.begin(), s.end()); }
    if (v.contains(U"encoding")) out.encoding = v.at(U"encoding").string;
    if (v.contains(U"lines")) {
        out.lines.emplace();
        for (const auto& line : v.at(U"lines").array) out.lines->push_back(line.string);
    }
    return out;
}
std::vector<std::u32string> strings_of(const Value& v) {
    std::vector<std::u32string> out;
    for (const auto& s : v.array) out.push_back(s.string);
    return out;
}
struct Built {
    Value value;
    std::string sha256;
    std::vector<std::pair<std::u32string, Script>> scripts;
};
Built build(const fs::path& source, std::u32string_view hint) {
    auto root = store_paths::resolve_native(ci::source_spelling(source));
    Built out;
    auto diagnostics = array();
    diagnostics.array = {diagnostic(U"static-projection-only", U"Catalog observations do not certify engine, save, rank or mode semantics."),
        diagnostic(U"artwork-semantics-unread", U"Presentation assets may contain instructions not recovered by text projection."),
        diagnostic(U"text-encoding-unverified", U"Text uses a reversible Latin-1 byte projection; original code page is unknown.")};
    for (const char* name : {"_MENU.TXT", "_RES.TXT"}) {
        const auto label = latin1(name);
        auto ref = resolve_reference(root, U"HUNTDAT/" + label);
        if (ref.status == ReferenceStatus::found) {
            auto path = root / ci::native_units(*ref.path);
            constexpr std::uintmax_t limit = 8 * 1024 * 1024;
            if (size_of(path) > limit) throw Error("script too large");
            // Actual bytes are bounded just past the limit; the parser reports the excess.
            auto script = parse_script(ci::read_prefix(path, limit + 1), *ref.path);
            for (const auto& d : Access::value(script)->at(U"diagnostics").array) diagnostics.array.push_back(d);
            out.scripts.emplace_back(label, std::move(script));
        } else if (ref.status != ReferenceStatus::missing) {
            throw Error(std::string("ambiguous or unsafe script: ") + name);
        }
    }
    if (out.scripts.empty()) throw Error("no recognizable content/menu script");
    auto find = [&](std::u32string_view name) -> const Script* {
        for (const auto& s : out.scripts) if (s.first == name) return &s.second;
        return nullptr;
    };
    const Script& script = find(U"_MENU.TXT") ? *find(U"_MENU.TXT") : *find(U"_RES.TXT");
    const Script& game = find(U"_RES.TXT") ? *find(U"_RES.TXT") : script;
    const auto& owner = Access::value(script);
    const Value& tree = *owner;
    std::set<std::u32string> sections;
    for (const auto& node : Access::value(game)->at(U"tree").at(U"children").array) sections.insert(schema::casefold(node.at(U"name").string));
    bool older = std::any_of(std::begin(OLDER), std::end(OLDER), [&](const auto& s) { return sections.count(s); });
    bool newer = std::any_of(std::begin(NEWER), std::end(NEWER), [&](const auto& s) { return sections.count(s); });
    std::u32string detected = older && newer ? U"mixed-unresolved" : older ? U"mee-older" : newer ? U"mee-newer" : U"classic-syntax-family-unknown";
    std::u32string dialect;
    if (hint == U"iceage-triassic") dialect = hint;
    else if (hint != U"unknown") {
        dialect = hint;
        if ((detected == U"mee-older" || detected == U"mee-newer" || detected == U"mixed-unresolved") && detected != hint)
            diagnostics.array.push_back(diagnostic(U"dialect-conflict", U"Declared dialect conflicts with script evidence."));
    } else dialect = detected;
    if (detected == U"mee-older")
        diagnostics.array.push_back(diagnostic(U"older-mee-engine-gap", U"Audited modern engine does not dispatch older split character blocks."));
    std::vector<const Value*> prices;
    for (const auto* node : blocks_of(tree, U"prices")) for (const auto& a : node->at(U"attributes").array) prices.push_back(&a);
    auto by_price = [&](std::u32string_view key) {
        std::vector<const Value*> out;
        for (const auto* p : prices) if (schema::casefold(p->at(U"key").string) == key) out.push_back(p);
        return out;
    };
    Value entries[4] = {array(), array(), array(), array()};
    const std::u32string names[4] = {U"areas", U"licenses", U"weapons", U"equipment"};
    const auto& script_source = tree.at(U"source");
    auto entry_reference = [&](const std::u32string& reference) { return reference_value(resolve_reference(root, reference)); };
    auto entry_text = [&](const std::u32string& reference) { return text_value(text_reference(root, reference)); };
    for (auto [section, group] : {std::pair{U"characters", 1}, std::pair{U"weapons", 2}}) {
        auto parents = blocks_of(tree, section);
        for (std::size_t section_index = 0; section_index < parents.size(); ++section_index) {
            const auto& children = parents[section_index]->at(U"children").array;
            for (std::size_t source_index = 0; source_index < children.size(); ++source_index) {
                const auto& node = children[source_index];
                auto ai = attribute_of(owner, node, U"ai");
                const Integer* ai_value = ai ? std::get_if<Integer>(&*ai) : nullptr;
                if (group == 1 && (!ai_value || !at_least_ten(ai_value->decimal))) continue;
                auto index = entries[group].array.size();
                auto label = attribute_of(owner, node, U"name");
                auto entry = object();
                entry.object = {{U"id", string(names[group] + U":" + digits(index))}, {U"kind", string(group == 1 ? U"license" : U"weapon")},
                    {U"ordinal", number(index)}, {U"label", scalar_value(label)}, {U"source", script_source}, {U"line", node.at(U"line")},
                    {U"source_ordinal", number(source_index)}, {U"section_ordinal", number(section_index)},
                    {U"attributes", node.at(U"attributes")}, {U"nested_observations", node.at(U"children")}, {U"price", Value{}}};
                auto declared = array();
                for (auto key : {U"file", U"pic", U"thumbnail"}) {
                    auto value = attribute_of(owner, node, key);
                    auto text = value ? std::get_if<std::u32string>(&*value) : nullptr;
                    if (!text) continue;
                    auto item = object(); item.object = {{U"field", string(key)}};
                    for (auto& kv : entry_reference(*text).object) item.object.push_back(std::move(kv));
                    declared.array.push_back(std::move(item));
                }
                entry.object.emplace_back(U"declared_references", std::move(declared));
                auto references = array();
                if (group == 1) {
                    entry.object.emplace_back(U"ai", integer(ai_value->decimal));
                    entry.object.emplace_back(U"species_resolution", string(U"unresolved-license-may-cover-multiple-species"));
                    auto number_text = latin1(minus_nine(ai_value->decimal));
                    references.array = {entry_reference(U"HUNTDAT/MENU/PICS/DINO" + number_text + U".TGA"),
                        entry_text(U"HUNTDAT/MENU/TXT/DINO" + number_text + U".TXM"),
                        entry_text(U"HUNTDAT/MENU/TXT/DINO" + digits(index + 1) + U".TXM")};
                    entry.object.emplace_back(U"references", std::move(references));
                    entry.object.emplace_back(U"reference_policy", string(U"AI-based legacy candidate and ordinal candidate; neither establishes species identity"));
                } else {
                    references.array = {entry_reference(U"HUNTDAT/MENU/PICS/WEAPON" + digits(index + 1) + U".TGA"),
                        entry_text(U"HUNTDAT/MENU/TXT/WEAPON" + digits(index + 1) + U".TXT")};
                    entry.object.emplace_back(U"references", std::move(references));
                }
                auto id = entry.at(U"id");
                entries[group].array.push_back(std::move(entry));
                auto text = label ? std::get_if<std::u32string>(&*label) : nullptr;
                // str(label) of an integer has no letters; only None/empty/str labels can match.
                if (!label || (text && (text->empty() || instruction_like(*text)))) {
                    auto d = diagnostic(U"unusual-label", U"Blank, ambiguous or instruction-like label retained verbatim.");
                    d.object.emplace_back(U"entry_id", std::move(id));
                    diagnostics.array.push_back(std::move(d));
                }
            }
        }
    }
    std::vector<std::string> ais;
    for (const auto& e : entries[1].array) ais.push_back(e.at(U"ai").integer);
    std::sort(ais.begin(), ais.end(), numeric_less);
    ais.erase(std::unique(ais.begin(), ais.end()), ais.end());
    for (const auto& ai : ais) {
        auto identities = array();
        for (const auto& e : entries[1].array) if (e.at(U"ai").integer == ai) identities.array.push_back(e.at(U"id"));
        if (identities.array.size() > 1) {
            auto d = diagnostic(U"duplicate-ai", U"Ordered license identities are independent of AI.");
            d.object.emplace_back(U"ai", integer(ai)); d.object.emplace_back(U"entries", std::move(identities));
            diagnostics.array.push_back(std::move(d));
        }
    }
    for (auto [key, group] : {std::pair{U"dino", 1}, std::pair{U"weapon", 2}}) {
        std::size_t offset = 0;
        if (group == 1) {
            const auto& licenses = entries[1].array;
            auto first = std::find_if(licenses.begin(), licenses.end(), [](const Value& e) { return e.at(U"ai").integer == "10"; });
            offset = first == licenses.end() ? 0 : static_cast<std::size_t>(first - licenses.begin());
        }
        auto observed = by_price(key);
        for (std::size_t index = 0; index < observed.size(); ++index) {
            const auto& price = *observed[index];
            auto target = index + offset;
            auto value = scalar(price.at(U"raw").string);
            if (target < entries[group].array.size()) {
                auto& entry = entries[group].array[target];
                if (auto n = std::get_if<Integer>(&value)) field(entry, U"price") = integer(n->decimal);
                entry.object.emplace_back(U"price_source", price);
            } else {
                auto d = diagnostic(U"surplus-price", U"Price has no corresponding selectable definition; retained without indexing past catalog.");
                d.object.emplace_back(U"category", string(names[group])); d.object.emplace_back(U"ordinal", number(index));
                d.object.emplace_back(U"observation", price);
                diagnostics.array.push_back(std::move(d));
            }
        }
    }
    auto integer_price = [](const Value& price) {
        auto value = scalar(price.at(U"raw").string);
        auto n = std::get_if<Integer>(&value);
        return n ? integer(n->decimal) : Value{};
    };
    auto areas = by_price(U"area");
    for (std::size_t slot = 1; slot <= areas.size(); ++slot) {
        const auto& price = *areas[slot - 1];
        auto stem = U"area" + digits(slot);
        std::vector<std::u32string> candidates = slot == 6 ? std::vector<std::u32string>{U"external", stem} : std::vector<std::u32string>{stem};
        auto pairs = array(); std::vector<std::u32string> complete;
        for (const auto& candidate : candidates) {
            auto map = resolve_reference(root, U"HUNTDAT/AREAS/" + candidate + U".MAP");
            auto rsc = resolve_reference(root, U"HUNTDAT/AREAS/" + candidate + U".RSC");
            if (map.status == ReferenceStatus::found && rsc.status == ReferenceStatus::found) complete.push_back(candidate);
            auto pair = object(); pair.object = {{U"stem", string(candidate)}, {U"map", reference_value(map)}, {U"rsc", reference_value(rsc)}};
            pairs.array.push_back(std::move(pair));
        }
        auto description = text_reference(root, U"HUNTDAT/MENU/TXT/AREA" + digits(slot) + U".TXT");
        auto label = description.lines && !description.lines->empty() ? string(description.lines->front()) : Value{};
        auto entry = object();
        entry.object = {{U"id", string(U"areas:" + digits(slot - 1))}, {U"kind", string(U"advertised-area-slot")}, {U"ordinal", number(slot - 1)},
            {U"slot", number(slot)}, {U"label", std::move(label)}, {U"price", integer_price(price)}, {U"price_source", price},
            {U"source", script_source}, {U"line", price.at(U"line")}, {U"map_candidates", std::move(pairs)},
            {U"launch_stem", complete.size() == 1 ? string(complete.front()) : Value{}}};
        auto references = array();
        references.array = {text_value(description), entry_reference(U"HUNTDAT/MENU/PICS/AREA" + digits(slot) + U".TGA")};
        entry.object.emplace_back(U"references", std::move(references));
        auto id = entry.at(U"id");
        entries[0].array.push_back(std::move(entry));
        if (complete.size() != 1) {
            auto d = diagnostic(U"area-resource-unresolved", U"Advertised slot has missing or ambiguous MAP/RSC pair.");
            d.object.emplace_back(U"entry_id", std::move(id));
            diagnostics.array.push_back(std::move(d));
        }
    }
    if (!blocks_of(tree, U"areas").empty())
        diagnostics.array.push_back(diagnostic(U"explicit-areas-uninterpreted", U"Explicit area declarations retained in observations; adapter not established."));
    const std::u32string conventions[] = {U"camoflag", U"radar", U"scent", U"double"};
    auto accessories = by_price(U"acces");
    for (std::size_t index = 0; index < accessories.size(); ++index) {
        const auto& price = *accessories[index];
        std::vector<TextObservation> texts{text_reference(root, U"HUNTDAT/MENU/TXT/EQUIP" + digits(index + 1) + U".NFO")};
        auto references = array();
        references.array = {text_value(texts[0]), entry_reference(U"HUNTDAT/MENU/PICS/EQUIP" + digits(index + 1) + U".TGA")};
        if (index < 4) texts.push_back(text_reference(root, U"HUNTDAT/MENU/TXT/" + conventions[index] + U".NFO"));
        else if (accessories.size() == 5 && index == 4) texts.push_back(text_reference(root, U"HUNTDAT/MENU/TXT/TRANQ.NFO"));
        if (texts.size() == 2) references.array.push_back(text_value(texts[1]));
        auto entry = object();
        entry.object = {{U"id", string(U"equipment:" + digits(index))}, {U"kind", string(U"native-accessory-slot")}, {U"ordinal", number(index)},
            {U"label", Value{}}, {U"meaning", string(U"unresolved")}, {U"price", integer_price(price)}, {U"price_source", price},
            {U"source", script_source}, {U"line", price.at(U"line")}, {U"references", std::move(references)}};
        auto id = entry.at(U"id");
        entries[3].array.push_back(std::move(entry));
        std::set<std::string> digests;
        for (const auto& t : texts) if (t.sha256) digests.insert(*t.sha256);
        if (digests.size() > 1) {
            auto d = diagnostic(U"description-conflict", U"Competing accessory descriptions retained without precedence.");
            d.object.emplace_back(U"entry_id", std::move(id));
            diagnostics.array.push_back(std::move(d));
        }
    }
    diagnostics.array.push_back(diagnostic(U"equipment-semantics-unresolved", U"Accessory slots are observations; no extra equipment or mode is granted."));
    auto spelled_root = schema::path(store_paths::native_points(ci::source_spelling(root)), nt);
    auto walk = [&](const std::u32string& reference, auto&& keep) {
        std::vector<std::u32string> out;
        auto dir = resolve_reference(root, reference);
        if (dir.status != ReferenceStatus::found) return out;
        for (const auto& p : ci::walk_files(root / ci::native_units(*dir.path))) {
            if (keep(suffix(store_paths::native_points(p.filename())))) out.push_back(relative_posix(spelled_root, p));
        }
        return out;
    };
    auto physical = walk(U"HUNTDAT/AREAS", [](const std::u32string& s) { return s == U".map"; });
    auto descriptors = walk(U"HUNTDAT/AREAS", [](const std::u32string& s) { return s == U".c2map"; });
    if (!descriptors.empty()) {
        auto d = diagnostic(U"c2map-unvalidated", U"Descriptor files inventoried but not promoted to launchable hunts.");
        auto paths = array();
        for (auto& p : descriptors) paths.array.push_back(string(std::move(p)));
        d.object.emplace_back(U"paths", std::move(paths));
        diagnostics.array.push_back(std::move(d));
    }
    auto presentation = walk(U"HUNTDAT/MENU", [](const std::u32string& s) { return s == U".tga" || s == U".nfo" || s == U".txt" || s == U".txm"; });
    std::sort(physical.begin(), physical.end()); std::sort(presentation.begin(), presentation.end());
    auto starting = array();
    for (const auto* p : by_price(U"start")) starting.array.push_back(*p);
    auto modifiers = array();
    for (const auto& s : out.scripts) for (const auto* node : blocks_of(*Access::value(s.second), U"accessories")) modifiers.array.push_back(*node);
    auto scripts = object();
    for (const auto& s : out.scripts) scripts.object.emplace_back(s.first, *Access::value(s.second));
    auto dialect_value = object();
    dialect_value.object = {{U"hint", string(std::u32string(hint))}, {U"observed", string(detected)}, {U"effective", string(dialect)}, {U"engine_build", string(U"unknown")}};
    auto capabilities = object();
    capabilities.object = {{U"content_dialect_recognized", string(U"partial")}, {U"console_can_be_generated", string(U"partial")},
        {U"modern_engine_compatibility", string(detected == U"mee-older" ? U"known-dispatch-gap" : U"unknown")},
        {U"launch_tested", string(U"unknown")}, {U"hunt_save_round_trip_validated", string(U"unknown")},
        {U"trophy_interpretation_validated", string(U"unknown")}};
    auto hints = array(); hints.array.push_back(string(spelled_root.parts.empty() ? U"" : spelled_root.parts.back()));
    auto lists = [](std::vector<std::u32string>& items) { auto v = array(); for (auto& s : items) v.array.push_back(string(std::move(s))); return v; };
    const auto& sha = tree.at(U"sha256").string;
    out.sha256.assign(sha.begin(), sha.end());
    out.value = object();
    out.value.object = {{U"projection_version", number(1)}, {U"source", script_source}, {U"source_sha256", tree.at(U"sha256")},
        {U"dialect", std::move(dialect_value)}, {U"areas", std::move(entries[0])}, {U"licenses", std::move(entries[1])},
        {U"weapons", std::move(entries[2])}, {U"equipment", std::move(entries[3])}, {U"starting_score_observations", std::move(starting)},
        {U"score_modifier_observations", std::move(modifiers)}, {U"physical_maps", lists(physical)}, {U"presentation_references", lists(presentation)},
        {U"title", Value{}}, {U"title_hints", std::move(hints)}, {U"scripts", std::move(scripts)}, {U"diagnostics", std::move(diagnostics)},
        {U"capabilities", std::move(capabilities)}};
    return out;
}
} // namespace
TextObservation text_reference(const fs::path& source, std::u32string reference) {
    auto result = resolve_reference(source, std::move(reference));
    TextObservation out;
    out.reference = std::move(result.reference); out.status = text_status(result.status); out.path = std::move(result.path);
    if (result.status != ReferenceStatus::found) return out;
    // Path(root) / path uses the supplied spelling; resolution happened above.
    auto path = source / ci::native_units(*out.path);
    if (!regular(path)) return out;
    if (size_of(path) > 1024 * 1024) { out.status = TextStatus::too_large; return out; }
    // Actual bytes: a virtual zero-extent file still yields its real content.
    auto raw = ci::read_prefix(path, std::numeric_limits<std::size_t>::max());
    out.sha256 = c2::frontend::sha256(raw); out.encoding = U"latin1-byte-projection"; out.lines = splitlines(raw);
    return out;
}
std::string TextObservation::export_json() const { return compat::display(text_value(*this)); }
struct Entry::Impl { std::shared_ptr<const Value> value; };
struct Projection::Impl { std::shared_ptr<const Value> value; std::string sha256; std::vector<std::pair<std::u32string, Script>> scripts; };
struct ProjectionAccess {
    static Entry entry(std::shared_ptr<const Value> value) {
        auto p = std::make_shared<Entry::Impl>(); p->value = std::move(value);
        return Entry(std::move(p));
    }
    static Projection projection(Built built) {
        auto p = std::make_shared<Projection::Impl>();
        p->value = std::make_shared<const Value>(std::move(built.value)); p->sha256 = std::move(built.sha256); p->scripts = std::move(built.scripts);
        return Projection(std::move(p));
    }
};
Entry::Entry(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
Projection::Projection(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
const std::u32string& Entry::id() const noexcept { return impl_->value->at(U"id").string; }
const std::u32string& Entry::kind() const noexcept { return impl_->value->at(U"kind").string; }
std::size_t Entry::ordinal() const { return index_of(impl_->value->at(U"ordinal")); }
std::optional<Scalar> Entry::label() const {
    const auto& v = impl_->value->at(U"label");
    if (v.kind == Kind::null) return {};
    if (v.kind == Kind::integer) return Scalar{Integer{v.integer}};
    return Scalar{v.string};
}
std::optional<Integer> Entry::price() const { return integer_field(*impl_->value, U"price"); }
std::optional<Attribute> Entry::price_source() const {
    if (!impl_->value->contains(U"price_source")) return {};
    return attribute_from(impl_->value->at(U"price_source"));
}
const std::u32string& Entry::source() const noexcept { return impl_->value->at(U"source").string; }
std::size_t Entry::line() const { return index_of(impl_->value->at(U"line")); }
std::optional<Integer> Entry::ai() const { return integer_field(*impl_->value, U"ai"); }
std::optional<std::size_t> Entry::slot() const {
    if (!impl_->value->contains(U"slot")) return {};
    return index_of(impl_->value->at(U"slot"));
}
std::optional<std::u32string> Entry::launch_stem() const {
    if (!impl_->value->contains(U"launch_stem") || impl_->value->at(U"launch_stem").kind == Kind::null) return {};
    return impl_->value->at(U"launch_stem").string;
}
std::vector<TextObservation> Entry::references() const {
    std::vector<TextObservation> out;
    for (const auto& r : impl_->value->at(U"references").array) out.push_back(text_from(r));
    return out;
}
std::vector<DeclaredReference> Entry::declared_references() const {
    std::vector<DeclaredReference> out;
    if (!impl_->value->contains(U"declared_references")) return out;
    for (const auto& r : impl_->value->at(U"declared_references").array) out.push_back({r.at(U"field").string, reference_from(r)});
    return out;
}
std::vector<MapCandidate> Entry::map_candidates() const {
    std::vector<MapCandidate> out;
    if (!impl_->value->contains(U"map_candidates")) return out;
    for (const auto& c : impl_->value->at(U"map_candidates").array) out.push_back({c.at(U"stem").string, reference_from(c.at(U"map")), reference_from(c.at(U"rsc"))});
    return out;
}
std::string Entry::export_json() const { return compat::display(*impl_->value); }
std::size_t Projection::projection_version() const noexcept { return 1; }
const std::u32string& Projection::source() const noexcept { return impl_->value->at(U"source").string; }
const std::string& Projection::source_sha256() const noexcept { return impl_->sha256; }
Dialect Projection::dialect() const {
    const auto& d = impl_->value->at(U"dialect");
    return {d.at(U"hint").string, d.at(U"observed").string, d.at(U"effective").string, d.at(U"engine_build").string};
}
std::vector<Entry> Projection::entries(Group group) const {
    const auto key = group == Group::areas ? U"areas" : group == Group::licenses ? U"licenses" : group == Group::weapons ? U"weapons" : U"equipment";
    std::vector<Entry> out;
    for (const auto& e : impl_->value->at(key).array) out.push_back(ProjectionAccess::entry(std::shared_ptr<const Value>(impl_->value, &e)));
    return out;
}
std::vector<Attribute> Projection::starting_score_observations() const {
    std::vector<Attribute> out;
    for (const auto& a : impl_->value->at(U"starting_score_observations").array) out.push_back(attribute_from(a));
    return out;
}
std::vector<Node> Projection::score_modifier_observations() const {
    std::vector<Node> out;
    for (const auto& n : impl_->value->at(U"score_modifier_observations").array) out.push_back(Access::node(std::shared_ptr<const Value>(impl_->value, &n)));
    return out;
}
std::vector<std::u32string> Projection::physical_maps() const { return strings_of(impl_->value->at(U"physical_maps")); }
std::vector<std::u32string> Projection::presentation_references() const { return strings_of(impl_->value->at(U"presentation_references")); }
std::vector<std::u32string> Projection::title_hints() const { return strings_of(impl_->value->at(U"title_hints")); }
std::vector<std::pair<std::u32string, Script>> Projection::scripts() const { return impl_->scripts; }
std::vector<ProjectionDiagnostic> Projection::diagnostics() const {
    std::vector<ProjectionDiagnostic> out;
    for (const auto& v : impl_->value->at(U"diagnostics").array) {
        ProjectionDiagnostic d;
        d.code = v.at(U"code").string; d.message = v.at(U"message").string;
        if (v.contains(U"source")) d.source = v.at(U"source").string;
        if (v.contains(U"entry_id")) d.entry_id = v.at(U"entry_id").string;
        if (v.contains(U"category")) d.category = v.at(U"category").string;
        if (v.contains(U"line")) d.line = index_of(v.at(U"line"));
        if (v.contains(U"ordinal")) d.ordinal = index_of(v.at(U"ordinal"));
        d.ai = integer_field(v, U"ai");
        if (v.contains(U"entries")) d.entries = strings_of(v.at(U"entries"));
        if (v.contains(U"paths")) d.paths = strings_of(v.at(U"paths"));
        if (v.contains(U"observation")) d.observation = attribute_from(v.at(U"observation"));
        out.push_back(std::move(d));
    }
    return out;
}
std::vector<std::pair<std::u32string, std::u32string>> Projection::capabilities() const {
    std::vector<std::pair<std::u32string, std::u32string>> out;
    for (const auto& kv : impl_->value->at(U"capabilities").object) out.emplace_back(kv.first, kv.second.string);
    return out;
}
std::string Projection::export_json() const { return compat::display(*impl_->value); }
Projection project(const fs::path& root, std::u32string_view dialect_hint) { return ProjectionAccess::projection(build(root, dialect_hint)); }
}
