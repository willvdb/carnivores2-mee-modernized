// Test-only driver for session preparation, execution, reconciliation and
// recovery over the private API; disposable stores only.
#include "session_runner.hpp"
#include "c2/frontend/catalog.hpp"
#include "c2/frontend/content.hpp"
#include "c2/frontend/planning.hpp"
#include "catalog_internal.hpp"
#include "content_internal.hpp"
#include "planning_internal.hpp"
#include "probe_process.hpp"
#include "session_journal.hpp"
#include "store_paths.hpp"
#include <iostream>
#include <iterator>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
namespace fs = std::filesystem;
namespace {
using compat::Value;
std::string input() { return {std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()}; }
std::u32string wide(const std::string& s) { return store_paths::native_points(fs::u8path(s)); }
std::optional<fs::path> optional_path(const std::string& s) { return s == "-" ? std::nullopt : std::optional<fs::path>(fs::u8path(s)); }
std::u32string decimal(const Value& v) { return std::u32string(v.integer.begin(), v.integer.end()); }
// Clearly labelled asset-free policy doubles, mirrored byte for byte by the
// Python harness patches. They certify nothing about Genesis; production
// policies are exercised separately through their refusals.
Value observer_double(const Value&, const catalog::Projection&, const Value& slot, const Value& selection, const Value&) {
    using namespace planning_internal;
    Value argv = array_value();
    argv.array = {string_value(U"reg=" + decimal(slot)), ascii_value("prj=huntdat/areas/area1"), ascii_value("din=0"),
        ascii_value("wep=0"), string_value(U"dtm=" + decimal(selection.at(U"time_of_day"))), ascii_value("-observ"),
        string_value(std::u32string(planning::SCORE_MODIFIERS))};
    Value result = object_value();
    result.object = {{U"adapter", ascii_value("asset-free-policy-double")}, {U"candidate_argv", std::move(argv)}};
    return result;
}
Value hunt_double(const Value&, const catalog::Projection&, const Value& slot, const Value& selection, const Value&) {
    using namespace planning_internal;
    Value argv = array_value();
    argv.array = {string_value(U"reg=" + decimal(slot)), ascii_value("prj=huntdat/areas/area1"), ascii_value("din=1"),
        ascii_value("wep=1"), string_value(U"dtm=" + decimal(selection.at(U"time_of_day"))),
        string_value(std::u32string(planning::SCORE_MODIFIERS))};
    Value result = object_value();
    result.object = {{U"adapter", string_value(std::u32string(planning::HUNT_POLICY_ID))},
        {U"fixture_only", ascii_value("authored disposable state, NOT Genesis")}, {U"selection", selection},
        {U"candidate_argv", std::move(argv)}};
    return result;
}
session_policy::Policies policies(const std::string& choice) {
    session_policy::Policies result;
    if (choice == "fixture-policy") { result.observer = observer_double; result.hunt = hunt_double; }
    else if (choice != "production-policy") throw std::invalid_argument("policy selection must be fixture-policy or production-policy");
    return result;
}
native_session::Adapter adapter_named(const std::string& name, const Store& store) {
    if (name == "observer") return native_session::Adapter::observer;
    if (name == "hunt") return native_session::preparation_adapter(store); // native_hunt.prepare_hunt
    throw std::invalid_argument("adapter must be observer or hunt");
}
void ok(const Value& value) { std::cout << "ok " << compat::compact(value) << '\n'; }
} // namespace
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 2) return 2;
    const std::string command = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);
    try {
        if (command == "synthetic-fixture" && args.empty()) {
            std::cout << "ok " << sessions::synthetic_fixture().u8string() << '\n';
        } else if (command == "execution-spec" && args.size() == 1) {
            // execution-spec <root>; stdin {"scenario", "slot", "timeout"}
            const Value in = compat::parse(input());
            ok(sessions::execution_spec(fs::u8path(args[0]), in.at(U"scenario"), in.at(U"slot"), in.at(U"timeout")));
        } else if (command == "prepare-synthetic" && args.size() == 5) {
            // prepare-synthetic <store> <association> <area> <scenario> <probe|->; stdin {"time_of_day", "timeout"}
            const Value in = compat::parse(input());
            ok(sessions::prepare_session(Store(fs::u8path(args[0])), wide(args[1]), wide(args[2]), wide(args[3]),
                                         in.at(U"time_of_day"), in.at(U"timeout"), optional_path(args[4])));
        } else if (command == "trusted-engine" && args.size() == 2) {
            // trusted-engine <engine> <experimental 0|1>; stdin {"digest"}
            const Value in = compat::parse(input());
            ok(native_session::trusted_engine(fs::u8path(args[0]), in.at(U"digest"), args[1] == "1"));
        } else if (command == "query-contract" && args.size() == 1) {
            ok(native_session::query_contract(probe_process::executable_evidence(fs::u8path(args[0]))));
        } else if (command == "workspace-findings" && args.size() == 2) {
            ok(native_session::workspace_findings(fs::u8path(args[0]), args[1] == "1"));
        } else if (command == "native-pins" && args.size() == 5) {
            // native-pins <store> <observer|hunt|continuation> <association> <probe|-> <policy>; stdin {"selection", ["expected_codec"], ["generation"]}
            const Value in = compat::parse(input());
            const Store store(fs::u8path(args[0]));
            const auto adapter = args[1] == "continuation" ? native_session::Adapter::continuation
                : args[1] == "hunt" ? native_session::Adapter::hunt : native_session::Adapter::observer;
            const auto result = native_session::native_pins(adapter, store, wide(args[2]), in.at(U"selection"), optional_path(args[3]),
                in.contains(U"expected_codec") ? std::optional<Value>(in.at(U"expected_codec")) : std::nullopt, policies(args[4]),
                in.contains(U"generation") ? std::optional<std::u32string>(in.at(U"generation").string) : std::nullopt);
            ok(result.pins);
        } else if (command == "prepare-native" && args.size() == 5) {
            // prepare-native <store> <observer|hunt> <association> <probe|-> <policy>; stdin {"selection", "engine", "digest", "experimental", "timeout"}
            const Value in = compat::parse(input());
            const Store store(fs::u8path(args[0]));
            const auto adapter = adapter_named(args[1], store);
            const Value& experimental = in.at(U"experimental");
            ok(native_session::prepare(adapter, store, wide(args[2]), in.at(U"selection"),
                                       content_internal::native_units(in.at(U"engine").string), in.at(U"digest"),
                                       experimental.kind == compat::Kind::boolean && experimental.boolean, in.at(U"timeout"),
                                       optional_path(args[3]), policies(args[4])));
        } else if (command == "inspect" && args.size() == 2) {
            ok(session_journal::read(Store(fs::u8path(args[0])), wide(args[1])));
        } else return 2;
    } catch (const ResourceExhausted& e) { std::cout << "resource " << e.what() << '\n';
    } catch (const StoreError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const probe_process::ProbeError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const planning::Error& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const catalog::Error& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const ContentError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const fs::filesystem_error& e) { std::cout << "oserror " << e.code().value() << ' ' << e.what() << '\n';
    } catch (const std::invalid_argument& e) { std::cout << "invalid " << e.what() << '\n';
    } catch (const std::out_of_range& e) { std::cout << "indexerror " << e.what() << '\n';
    } catch (const compat::Error& e) { std::cout << "keyerror " << e.what() << '\n'; }
    return 0;
}
