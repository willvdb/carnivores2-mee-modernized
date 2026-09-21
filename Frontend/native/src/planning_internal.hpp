#pragma once
// PRIVATE planning representation and supplied-value seams. Never included
// from public headers. The value entry points evaluate the reference checks
// over arbitrary retained kinds so tests can reach branches that a validated
// manifest, a typed selection or the native codec cannot produce.
#include "c2/frontend/planning.hpp"
#include "json_compat.hpp"
namespace c2::frontend::planning {
struct Revision::Impl { compat::Value value; };
struct Selection::Impl { compat::Value value; };
struct GenesisPlan::Impl { compat::Value value; };
struct PlanningAccess {
    static Revision revision(compat::Value v) { auto p = std::make_shared<Revision::Impl>(); p->value = std::move(v); return Revision(std::move(p)); }
    static Selection selection(compat::Value v) { auto p = std::make_shared<Selection::Impl>(); p->value = std::move(v); return Selection(std::move(p)); }
    static GenesisPlan plan(compat::Value v) { auto p = std::make_shared<GenesisPlan::Impl>(); p->value = std::move(v); return GenesisPlan(std::move(p)); }
    static const compat::Value& value(const Revision& r) { return r.impl_->value; }
    static const compat::Value& value(const Selection& s) { return s.impl_->value; }
    static const compat::Value& value(const GenesisPlan& p) { return p.impl_->value; }
};
}
namespace c2::frontend::planning_internal {
using compat::Value;
// Python int semantics over canonical decimals: optional '-', no leading zeros, no "-0".
bool canonical_decimal(std::string_view);
int compare_decimal(std::string_view a, std::string_view b); // -1, 0, 1
std::string add_decimal(std::string_view a, std::string_view b); // non-negative operands
// Python ordering of an int against an int/bool/float value: -1/0/1, nullopt
// for NaN (every comparison false). Other kinds throw std::invalid_argument
// as the reference raises TypeError.
std::optional<int> compare_number(std::string_view decimal, const Value& number);
// Value helpers shared by the policy TUs.
Value string_value(std::u32string);
Value ascii_value(std::string_view);
Value integer_value(std::string);
Value boolean_value(bool);
Value array_value();
Value object_value();
Value diagnostic_value(std::u32string_view code, std::u32string_view message);
bool is_text(const Value&, std::u32string_view);
// Supplied-value policy evaluation in the reference argument order.
Value observer_policy(const Value& revision, const catalog::Projection&, const Value& slot,
                      const Value& selection, const Value& score);
Value hunt_policy(const Value& revision, const catalog::Projection&, const Value& slot,
                  const Value& selection, const Value& score);
}
