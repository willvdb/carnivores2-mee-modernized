// Genesis observer/hunt policies over supplied observations (lodge.genesis,
// lodge.genesis_hunt). Every reference check is evaluated over the retained
// values, including defensive ones a fresh native projection cannot fail.
#include "c2/frontend/planning.hpp"
#include "planning_internal.hpp"
#include "catalog_internal.hpp"
#include "discovery_internal.hpp"
#include "schema_compat.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <set>

namespace c2::frontend::planning_internal {
using compat::Kind;
using planning::Error;
bool canonical_decimal(std::string_view s) {
    std::size_t start = !s.empty() && s[0] == '-' ? 1 : 0;
    if (start == s.size()) return false;
    for (auto i = start; i < s.size(); ++i) if (s[i] < '0' || s[i] > '9') return false;
    if (s[start] == '0' && (s.size() - start > 1 || start)) return false;
    return true;
}
int compare_decimal(std::string_view a, std::string_view b) {
    const bool na = a[0] == '-', nb = b[0] == '-';
    if (na != nb) return na ? -1 : 1;
    auto ma = na ? a.substr(1) : a, mb = nb ? b.substr(1) : b;
    int c = ma.size() != mb.size() ? (ma.size() < mb.size() ? -1 : 1) : ma == mb ? 0 : ma < mb ? -1 : 1;
    return na ? -c : c;
}
std::string add_decimal(std::string_view a, std::string_view b) {
    std::string out;
    int carry = 0;
    for (std::size_t i = a.size(), j = b.size(); i || j || carry;) {
        int v = carry + (i ? a[--i] - '0' : 0) + (j ? b[--j] - '0' : 0);
        out.push_back(static_cast<char>('0' + v % 10));
        carry = v / 10;
    }
    std::reverse(out.begin(), out.end());
    return out.empty() ? "0" : out;
}
namespace {
// Exact decimal of an integral finite binary64, without a rounded conversion.
std::string float_integer(double x) {
    if (x == 0) return "0";
    const bool negative = std::signbit(x);
    x = std::fabs(x);
    int exponent = 0;
    const double f = std::frexp(x, &exponent);
    auto mantissa = static_cast<std::uint64_t>(std::ldexp(f, 53));
    exponent -= 53;
    if (exponent < 0) { mantissa >>= -exponent; exponent = 0; }
    std::string s = std::to_string(mantissa);
    while (exponent-- > 0) {
        int carry = 0;
        for (auto i = s.rbegin(); i != s.rend(); ++i) {
            int v = (*i - '0') * 2 + carry;
            *i = static_cast<char>('0' + v % 10);
            carry = v / 10;
        }
        if (carry) s.insert(s.begin(), '1');
    }
    return negative ? "-" + s : s;
}
}
std::optional<int> compare_number(std::string_view decimal, const Value& number) {
    switch (number.kind) {
    case Kind::integer: return compare_decimal(decimal, number.integer);
    case Kind::boolean: return compare_decimal(decimal, number.boolean ? "1" : "0");
    case Kind::floating: {
        const double f = number.floating;
        if (std::isnan(f)) return std::nullopt;
        if (std::isinf(f)) return f > 0 ? -1 : 1;
        const double floor = std::floor(f);
        const int c = compare_decimal(decimal, float_integer(floor));
        // A fractional value lies strictly between floor and floor + 1.
        if (floor != f) return c <= 0 ? -1 : 1;
        return c;
    }
    default: throw std::invalid_argument("unorderable kinds: int and non-number");
    }
}
Value string_value(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
Value ascii_value(std::string_view s) { return string_value({s.begin(), s.end()}); }
Value integer_value(std::string decimal) { Value v; v.kind = Kind::integer; v.integer = std::move(decimal); return v; }
Value boolean_value(bool b) { Value v; v.kind = Kind::boolean; v.boolean = b; return v; }
Value array_value() { Value v; v.kind = Kind::array; return v; }
Value object_value() { Value v; v.kind = Kind::object; return v; }
Value diagnostic_value(std::u32string_view code, std::u32string_view message) {
    auto v = object_value();
    v.object = {{U"code", string_value(std::u32string(code))}, {U"message", string_value(std::u32string(message))}};
    return v;
}
bool is_text(const Value& v, std::u32string_view text) { return v.kind == Kind::string && v.string == text; }
namespace {
std::u32string digits(std::size_t n) { auto s = std::to_string(n); return {s.begin(), s.end()}; }
std::u32string decimal_text(const std::string& decimal) { return {decimal.begin(), decimal.end()}; }
Value pinned_revision() {
    auto v = object_value();
    v.object = {{U"algorithm", string_value(std::u32string(planning::GENESIS_REVISION.algorithm))},
        {U"sha256", string_value(std::u32string(planning::GENESIS_REVISION.sha256))},
        {U"file_count", integer_value(std::to_string(planning::GENESIS_REVISION.file_count))},
        {U"byte_count", integer_value(std::to_string(planning::GENESIS_REVISION.byte_count))}};
    return v;
}
const std::pair<const char32_t*, std::size_t> EXPECTED[] = {
    {U"areas", planning::EXPECTED_COUNTS.areas}, {U"licenses", planning::EXPECTED_COUNTS.licenses},
    {U"weapons", planning::EXPECTED_COUNTS.weapons}, {U"equipment", planning::EXPECTED_COUNTS.equipment},
    {U"physical_maps", planning::EXPECTED_COUNTS.physical_maps}};
const std::u32string SELECTION_KEYS[] = {U"area", U"mode", U"time_of_day", U"licenses", U"weapons", U"equipment"};
bool exact_int(const Value& v) { return v.kind == Kind::integer; }
bool in_range_eight(const Value& slot) { return compare_decimal(slot.integer, "0") >= 0 && compare_decimal(slot.integer, "7") <= 0; }
bool time_of_day_ok(const Value& v) { return exact_int(v) && (v.integer == "0" || v.integer == "1" || v.integer == "2"); }
bool hashable(const Value& v) { return v.kind != Kind::array && v.kind != Kind::object; }
// set(selection) != {'area', 'mode', 'time_of_day', 'licenses', 'weapons', 'equipment'}
// over the retained kind: dict keys, list/tuple elements or str characters.
// Non-iterables and unhashable elements raise TypeError in the reference.
bool selection_keys_match(const Value& selection) {
    std::set<std::u32string> names;
    if (selection.kind == Kind::object) {
        for (const auto& kv : selection.object) names.insert(kv.first);
    } else if (selection.kind == Kind::array) {
        // The whole set is built before comparison, so any unhashable element raises.
        for (const auto& item : selection.array) if (!hashable(item)) throw std::invalid_argument("unhashable selection element");
        for (const auto& item : selection.array) {
            if (item.kind != Kind::string) return false;
            names.insert(item.string);
        }
    } else if (selection.kind == Kind::string) {
        return false; // single characters never spell a key name
    } else throw std::invalid_argument("selection is not iterable");
    if (names.size() != 6) return false;
    for (const auto& key : SELECTION_KEYS) if (!names.count(key)) return false;
    return true;
}
const Value& item(const Value& selection, std::u32string_view key) {
    if (selection.kind != Kind::object) throw std::invalid_argument("selection indices must be integers");
    return selection.at(key);
}
bool fullmatch_area_stem(const std::u32string& stem) {
    if (stem == U"external") return true;
    return stem.size() == 5 && stem.compare(0, 4, U"area") == 0 && stem[4] >= U'1' && stem[4] <= U'8';
}
bool has_code(const Value& diagnostics, std::initializer_list<std::u32string_view> codes) {
    for (const auto& d : diagnostics.array) {
        const auto& code = d.at(U"code");
        for (auto c : codes) if (is_text(code, c)) return true;
    }
    return false;
}
void check_structure(const Value& catalog, const char* revision_message, const char* dialect_message, const char* counts_message, const Value& revision) {
    if (!schema::equal(revision, pinned_revision())) throw Error(revision_message);
    const auto& dialect = catalog.at(U"dialect");
    if (!is_text(dialect.at(U"observed"), U"mee-newer") || !is_text(dialect.at(U"effective"), U"mee-newer")) throw Error(dialect_message);
    for (const auto& [key, count] : EXPECTED) if (catalog.at(key).array.size() != count) throw Error(counts_message);
}
Value argv_value(std::initializer_list<std::u32string> items) {
    auto v = array_value();
    for (const auto& s : items) v.array.push_back(string_value(s));
    return v;
}
}
Value observer_policy(const Value& revision, const catalog::Projection& projection, const Value& slot, const Value& selection, const Value& score) {
    const Value& catalog = *catalog::projection_value(projection);
    check_structure(catalog, "Genesis policy refuses an unpinned content revision",
        "Genesis policy requires observed current-MEE script evidence", "Genesis catalog differs from pinned audit structure", revision);
    const char* loadout = "Genesis v1 permits only observer, no loadout, native slots 0..7 and dawn/day/night";
    if (!exact_int(slot) || !in_range_eight(slot) || !selection_keys_match(selection)) throw Error(loadout);
    if (!is_text(item(selection, U"mode"), U"observer") || schema::truth(item(selection, U"licenses"))
        || schema::truth(item(selection, U"weapons")) || schema::truth(item(selection, U"equipment"))
        || !time_of_day_ok(item(selection, U"time_of_day"))) throw Error(loadout);
    if (schema::truth(catalog.at(U"score_modifier_observations"))) throw Error("accessory overrides differ from pinned Genesis evidence");
    if (has_code(catalog.at(U"diagnostics"), {U"unclosed-block", U"unmatched-brace", U"dialect-conflict", U"explicit-areas-uninterpreted"}))
        throw Error("Genesis catalog ambiguity requires review");
    const Value* area = nullptr;
    for (const auto& a : catalog.at(U"areas").array) if (schema::equal(a.at(U"id"), item(selection, U"area"))) { area = &a; break; }
    // A found entry is a non-empty dict, so `not area` is only the absent case.
    if (!area || area->at(U"launch_stem").kind != Kind::string || !fullmatch_area_stem(area->at(U"launch_stem").string))
        throw Error("Genesis observer requires a single evidenced area resource pair");
    const Value& cost = area->at(U"price");
    if (!exact_int(cost) || compare_decimal(cost.integer, "0") < 0 || !exact_int(score) || compare_decimal(score.integer, cost.integer) < 0)
        throw Error("Genesis observer does not meet the observed area score requirement");
    // Both pinned scripts have no accessories {} override; these six defaults
    // are evidenced in Menu/Resources.cpp and Hunt/Game/EngineInit.cpp.
    auto argv = argv_value({U"reg=" + decimal_text(slot.integer), U"prj=huntdat/areas/" + area->at(U"launch_stem").string,
        U"din=0", U"wep=0", U"dtm=" + decimal_text(item(selection, U"time_of_day").integer), U"-observ", std::u32string(planning::SCORE_MODIFIERS)});
    auto capabilities = object_value();
    capabilities.object = {{U"genesis_policy", string_value(U"structurally-validated")}, {U"modern_engine_compatibility", string_value(U"unknown")},
        {U"engine_process_executed", boolean_value(false)}, {U"observer_session_launched", boolean_value(false)},
        {U"hunt_save_round_trip_validated", boolean_value(false)}};
    auto diagnostics = array_value();
    for (auto code : {U"experimental-native-validation-required", U"engine-build-not-certified", U"progression-semantics-unverified"}) {
        auto d = object_value(); d.object = {{U"code", string_value(code)}}; diagnostics.array.push_back(std::move(d));
    }
    auto out = object_value();
    out.object = {{U"adapter", string_value(std::u32string(planning::OBSERVER_POLICY_ID))}, {U"revision", pinned_revision()},
        {U"selection", selection}, {U"native_slot", slot}, {U"candidate_argv", std::move(argv)}, {U"score_requirement", cost},
        {U"score_mutation", string_value(U"none")}, {U"rank_policy", string_value(U"unverified")},
        {U"equipment_policy", string_value(U"all-disabled")}, {U"mask_policy", string_value(U"observer-zero-only")},
        {U"process_launch_allowed", boolean_value(false)}, {U"capabilities", std::move(capabilities)}, {U"diagnostics", std::move(diagnostics)}};
    return out;
}
Value hunt_policy(const Value& revision, const catalog::Projection& projection, const Value& slot, const Value& selection, const Value& score) {
    const Value& catalog = *catalog::projection_value(projection);
    check_structure(catalog, "Genesis hunt refuses an unpinned content revision",
        "Genesis hunt requires observed current-MEE script evidence", "Genesis hunt catalog differs from pinned audit structure", revision);
    const char* shape = "hunt v1 requires one area, dawn/day/night, slot 0..7 and no equipment or flags";
    if (!exact_int(slot) || !in_range_eight(slot) || selection.kind != Kind::object || !selection_keys_match(selection)) throw Error(shape);
    if (!is_text(selection.at(U"mode"), U"hunt") || selection.at(U"area").kind != Kind::string || !time_of_day_ok(selection.at(U"time_of_day"))
        || selection.at(U"equipment").kind != Kind::array || schema::truth(selection.at(U"equipment"))) throw Error(shape);
    for (auto group : {U"licenses", U"weapons"}) {
        const auto& ids = selection.at(group);
        if (ids.kind != Kind::array || ids.array.size() != 1 || ids.array[0].kind != Kind::string)
            throw Error("hunt v1 requires exactly one catalog license and one weapon");
    }
    if (schema::truth(catalog.at(U"score_modifier_observations")) || has_code(catalog.at(U"diagnostics"),
            {U"unclosed-block", U"unmatched-brace", U"dialect-conflict", U"explicit-areas-uninterpreted", U"surplus-price"}))
        throw Error("ambiguous Genesis catalog/price/modifier policy requires review");
    const Value* chosen[3] = {nullptr, nullptr, nullptr};
    std::size_t ordinals[3] = {0, 0, 0};
    const std::u32string groups[3] = {U"areas", U"licenses", U"weapons"};
    for (std::size_t g = 0; g < 3; ++g) {
        const auto& entries = catalog.at(groups[g]).array;
        // A fresh projection always retains canonical contiguous identities; the
        // check is kept as the reference keeps it, over the retained values.
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            if (entry.kind != Kind::object || !entry.contains(U"id") || !is_text(entry.at(U"id"), groups[g] + U":" + digits(i))
                || !entry.contains(U"ordinal") || !exact_int(entry.at(U"ordinal")) || entry.at(U"ordinal").integer != std::to_string(i))
                throw Error("ambiguous or stale catalog ordering");
        }
        const Value& identity = g == 0 ? selection.at(U"area") : selection.at(groups[g]).array[0];
        std::size_t matches = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) if (schema::equal(entries[i].at(U"id"), identity)) { if (!matches++) { chosen[g] = &entries[i]; ordinals[g] = i; } }
        if (matches != 1) throw Error("unknown or ambiguous catalog selection");
    }
    const Value &area = *chosen[0], &license = *chosen[1], &weapon = *chosen[2];
    const Value* stem = area.contains(U"launch_stem") ? &area.at(U"launch_stem") : nullptr;
    if (!stem || stem->kind != Kind::string || !fullmatch_area_stem(stem->string)
        || (stem->string != U"area" + digits(ordinals[0] + 1) && !(ordinals[0] == 5 && stem->string == U"external")))
        throw Error("selected area has no unambiguous supported resource pair");
    for (const Value* entry : {&license, &weapon}) {
        const Value* label = entry->contains(U"label") ? &entry->at(U"label") : nullptr;
        if (!label || label->kind != Kind::string || schema::blank(label->string)) throw Error("selected catalog entry has an unresolved label");
    }
    const Value* ai = license.contains(U"ai") ? &license.at(U"ai") : nullptr;
    if (!ai || !exact_int(*ai) || compare_decimal(ai->integer, "10") < 0) throw Error("selected catalog entry is not a huntable license");
    std::string cost = "0";
    for (const Value* entry : {&area, &license, &weapon}) {
        const Value* price = entry->contains(U"price") ? &entry->at(U"price") : nullptr;
        if (!price || !exact_int(*price) || compare_decimal(price->integer, "0") < 0 || compare_decimal(price->integer, "2147483647") > 0)
            throw Error("unresolved or out-of-range listed price");
    }
    for (const Value* entry : {&area, &license, &weapon}) cost = add_decimal(cost, entry->at(U"price").integer);
    if (!exact_int(score) || compare_decimal(score.integer, "0") < 0 || compare_decimal(score.integer, "2147483647") > 0 || compare_decimal(cost, score.integer) > 0)
        throw Error("selection exceeds the native score requirement");
    // Menu/Menu.cpp emits unshifted positional bits; ordinals are contiguous
    // within the pinned counts (9 licenses, 8 weapons), so the shift is bounded.
    if (ordinals[1] >= 31 || ordinals[2] >= 31) throw std::logic_error("Genesis ordinal outside the pinned counts");
    const std::uint32_t din = std::uint32_t{1} << ordinals[1], wep = std::uint32_t{1} << ordinals[2];
    auto argv = argv_value({U"reg=" + decimal_text(slot.integer), U"prj=huntdat/areas/" + stem->string,
        U"din=" + digits(din), U"wep=" + digits(wep), U"dtm=" + decimal_text(selection.at(U"time_of_day").integer),
        std::u32string(planning::SCORE_MODIFIERS)});
    auto diagnostics = array_value();
    for (auto code : {U"experimental-native-validation-required", U"native-menu-rank-disagreement-preserved"}) {
        auto d = object_value(); d.object = {{U"code", string_value(code)}}; diagnostics.array.push_back(std::move(d));
    }
    auto out = object_value();
    out.object = {{U"adapter", string_value(std::u32string(planning::HUNT_POLICY_ID))}, {U"revision", pinned_revision()},
        {U"selection", selection}, {U"native_slot", slot}, {U"candidate_argv", std::move(argv)},
        {U"license_mask", integer_value(std::to_string(din))}, {U"weapon_mask", integer_value(std::to_string(wep))},
        {U"score_requirement", integer_value(cost)}, {U"score_mutation", string_value(U"none")},
        {U"rank_policy", string_value(U"native-observation-no-rank-gate")}, {U"equipment_policy", string_value(U"all-disabled")},
        {U"mask_policy", string_value(U"single-ordinal-bit")}, {U"process_launch_allowed", boolean_value(false)},
        {U"diagnostics", std::move(diagnostics)}};
    return out;
}
}
namespace c2::frontend::planning {
using compat::Kind;
using compat::Value;
namespace pi = planning_internal;
namespace {
Value checked_integer(const Integer& n) {
    if (!pi::canonical_decimal(n.decimal)) throw std::invalid_argument("Integer is not a canonical decimal");
    return pi::integer_value(n.decimal);
}
Value score_value(const std::optional<Integer>& score) { return score ? checked_integer(*score) : Value{}; }
Value strings(const std::vector<std::u32string>& items) {
    auto v = pi::array_value();
    for (const auto& s : items) v.array.push_back(pi::string_value(s));
    return v;
}
std::vector<std::u32string> texts(const Value& array) {
    std::vector<std::u32string> out;
    for (const auto& v : array.array) out.push_back(v.string);
    return out;
}
std::vector<std::u32string> codes(const Value& diagnostics) {
    std::vector<std::u32string> out;
    for (const auto& d : diagnostics.array) out.push_back(d.at(U"code").string);
    return out;
}
}
Revision::Revision(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
std::string Revision::export_json() const { return compat::display(impl_->value); }
Revision revision_of(const InstanceObservation& instance) { return PlanningAccess::revision(discovery_internal::instance_value(instance).at(U"revision")); }
Selection::Selection(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
std::string Selection::export_json() const { return compat::display(impl_->value); }
Selection Selection::observer(std::u32string area, Integer time_of_day) {
    auto v = pi::object_value();
    v.object = {{U"area", pi::string_value(std::move(area))}, {U"mode", pi::string_value(U"observer")}, {U"time_of_day", checked_integer(time_of_day)},
        {U"licenses", pi::array_value()}, {U"weapons", pi::array_value()}, {U"equipment", pi::array_value()}};
    return PlanningAccess::selection(std::move(v));
}
Selection Selection::hunt(std::u32string area, std::vector<std::u32string> licenses, std::vector<std::u32string> weapons, Integer time_of_day) {
    auto v = pi::object_value();
    v.object = {{U"area", pi::string_value(std::move(area))}, {U"licenses", strings(licenses)}, {U"weapons", strings(weapons)},
        {U"equipment", pi::array_value()}, {U"mode", pi::string_value(U"hunt")}, {U"time_of_day", checked_integer(time_of_day)}};
    return PlanningAccess::selection(std::move(v));
}
GenesisPlan::GenesisPlan(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
const std::u32string& GenesisPlan::adapter() const noexcept { return impl_->value.at(U"adapter").string; }
std::vector<std::u32string> GenesisPlan::candidate_argv() const { return texts(impl_->value.at(U"candidate_argv")); }
Integer GenesisPlan::score_requirement() const { return Integer{impl_->value.at(U"score_requirement").integer}; }
std::optional<Integer> GenesisPlan::license_mask() const {
    return impl_->value.contains(U"license_mask") ? std::optional<Integer>(Integer{impl_->value.at(U"license_mask").integer}) : std::nullopt;
}
std::optional<Integer> GenesisPlan::weapon_mask() const {
    return impl_->value.contains(U"weapon_mask") ? std::optional<Integer>(Integer{impl_->value.at(U"weapon_mask").integer}) : std::nullopt;
}
bool GenesisPlan::process_launch_allowed() const noexcept { return impl_->value.at(U"process_launch_allowed").boolean; }
std::vector<std::u32string> GenesisPlan::diagnostic_codes() const { return codes(impl_->value.at(U"diagnostics")); }
std::string GenesisPlan::export_json() const { return compat::display(impl_->value); }
GenesisPlan observer_policy(const Revision& revision, const catalog::Projection& catalog, const Integer& slot, const Selection& selection, const std::optional<Integer>& score) {
    return PlanningAccess::plan(pi::observer_policy(PlanningAccess::value(revision), catalog, checked_integer(slot), PlanningAccess::value(selection), score_value(score)));
}
GenesisPlan hunt_policy(const Revision& revision, const catalog::Projection& catalog, const Integer& slot, const Selection& selection, const std::optional<Integer>& score) {
    return PlanningAccess::plan(pi::hunt_policy(PlanningAccess::value(revision), catalog, checked_integer(slot), PlanningAccess::value(selection), score_value(score)));
}
}
