// TEST-ONLY native frontend: the production dispatcher with the explicitly
// labelled asset-free policy doubles used by the Python reference tests
// (test_native_observer / test_acceptance_fixtures). Built only with
// BUILD_TESTING; it is never staged or installed. It certifies nothing about
// Genesis content; real policy refusals are exercised through c2-frontend-native.
#include "cli.hpp"
#include "planning_internal.hpp"
#include <iostream>
namespace {
using namespace c2::frontend;
using compat::Value;
Value text(std::string_view s) { return planning_internal::ascii_value(s); }
Value argv(const Value& slot, const Value& selection, bool observer) {
    auto list = planning_internal::array_value();
    const auto& time = selection.at(U"time_of_day").integer;
    for (const std::string& a : {"reg=" + slot.integer, std::string("prj=huntdat/areas/area1"),
             std::string(observer ? "din=0" : "din=1"), std::string(observer ? "wep=0" : "wep=1"), "dtm=" + time})
        list.array.push_back(text(a));
    if (observer) list.array.push_back(text("-observ"));
    list.array.push_back(text("smod=0.85,0.70,0.80,1.0,1.25,1.0"));
    return list;
}
}
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
int wmain(int argc, wchar_t** argv_) {
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#else
int main(int argc, char** argv_) {
#endif
    cli::Seams seams;
    seams.policies.observer = [](const Value&, const catalog::Projection&, const Value& slot, const Value& selection,
                                 const Value&) {
        auto result = planning_internal::object_value();
        result.object = {{U"adapter", text("asset-free-policy-double")}, {U"candidate_argv", argv(slot, selection, true)}};
        return result;
    };
    seams.policies.hunt = [](const Value&, const catalog::Projection&, const Value& slot, const Value& selection,
                             const Value&) {
        auto result = planning_internal::object_value();
        result.object = {{U"adapter", planning_internal::string_value(std::u32string(planning::HUNT_POLICY_ID))},
            {U"fixture_only", text("authored disposable state, NOT Genesis")}, {U"selection", selection},
            {U"candidate_argv", argv(slot, selection, false)}};
        return result;
    };
    std::vector<std::filesystem::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv_[i]);
    return cli::run(args, std::cout, std::cerr, seams);
}
