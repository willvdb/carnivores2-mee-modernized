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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
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
        } else if (command == "run" && (args.size() == 3 || args.size() == 4)) {
            // run <store> <id> <probe|-> [fail-running]: session_runner.run_session without native
            // authorization; fail-running injects a replacement failure into the second journal
            // write of the run (the 'running' transition), like the reference test's patched transition.
            store_write::FailureHook hook;
            int replacements = 0;
            if (args.size() == 4 && args[3] == "fail-running") hook = [&replacements](store_write::WritePhase phase, const fs::path& target) {
                if (phase == store_write::WritePhase::replace && target.filename() == "journal.json" && ++replacements == 2)
                    throw fs::filesystem_error("simulated disk failure", target, std::make_error_code(std::errc::io_error));
            };
            ok(session_runner::run_session(Store(fs::u8path(args[0])), wide(args[1]), optional_path(args[2]), nullptr, std::nullopt, {}, nullptr, hook));
        } else if (command == "run-interrupt" && args.size() == 4) {
            // run-interrupt <store> <id> <probe|-> <before-launch|before-spawn|while-running>: the CLI's
            // SIGINT flag set before the call, from the first journal write of the run (the launching
            // transition, so it is observed between that write and the spawn), or from the second
            // (the running transition, so the child has already been spawned).
            std::atomic<bool> interrupt{args[3] == "before-launch"};
            const int at = args[3] == "before-spawn" ? 1 : args[3] == "while-running" ? 2 : 0;
            int replacements = 0;
            store_write::FailureHook hook;
            if (at) hook = [&interrupt, &replacements, at](store_write::WritePhase phase, const fs::path& target) {
                if (phase == store_write::WritePhase::replace && target.filename() == "journal.json" && ++replacements == at)
                    interrupt.store(true);
            };
            ok(session_runner::run_session(Store(fs::u8path(args[0])), wide(args[1]), optional_path(args[2]), nullptr, std::nullopt, {}, &interrupt, hook));
        } else if (command == "run-native-interrupt" && args.size() == 5) {
            // run-native-interrupt <store> <observer|hunt> <id> <probe|-> <policy>: pre-set interrupt; stdin {"engine", "digest"}
            const Value in = compat::parse(input());
            std::atomic<bool> interrupt{true};
            ok(session_runner::run_native(args[1] == "observer" ? std::optional<native_session::Adapter>(native_session::Adapter::observer) : std::nullopt,
                                          args[1] == "hunt", Store(fs::u8path(args[0])), wide(args[2]),
                                          content_internal::native_units(in.at(U"engine").string), in.at(U"digest"),
                                          true, optional_path(args[3]), nullptr, policies(args[4]), &interrupt));
        } else if (command == "run-cancel" && args.size() == 4) {
            // run-cancel <store> <id> <probe|-> <cancel-after-ms>: synthetic run cancelled by the flag.
            std::atomic<bool> cancel{false};
            std::thread timer([&cancel, delay = std::stoi(args[3])] {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                cancel.store(true);
            });
            struct Join { std::thread& t; ~Join() { if (t.joinable()) t.join(); } } join{timer};
            ok(session_runner::run_session(Store(fs::u8path(args[0])), wide(args[1]), optional_path(args[2]), &cancel, std::nullopt, {}));
        } else if (command == "run-cancel-after-running" && args.size() == 4) {
            // run-cancel-after-running <store> <id> <probe|-> <ms>: synthetic run cancelled <ms> after
            // the 'running' transition (the second journal write of the run, made once the child is
            // spawned), so preflight and spawn time never consume the delay.
            std::atomic<bool> cancel{false};
            std::mutex mutex;
            std::condition_variable changed;
            bool running = false, finished = false;
            int replacements = 0;
            const store_write::FailureHook hook = [&](store_write::WritePhase phase, const fs::path& target) {
                if (phase == store_write::WritePhase::replace && target.filename() == "journal.json" && ++replacements == 2) {
                    { std::lock_guard<std::mutex> guard(mutex); running = true; }
                    changed.notify_all();
                }
            };
            std::thread timer([&, delay = std::stoi(args[3])] {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return running || finished; });
                if (running && !changed.wait_for(lock, std::chrono::milliseconds(delay), [&] { return finished; }))
                    cancel.store(true);
            });
            struct Finish {
                std::mutex& mutex; std::condition_variable& changed; bool& finished; std::thread& timer;
                ~Finish() {
                    { std::lock_guard<std::mutex> guard(mutex); finished = true; }
                    changed.notify_all();
                    timer.join();
                }
            } finish{mutex, changed, finished, timer};
            ok(session_runner::run_session(Store(fs::u8path(args[0])), wide(args[1]), optional_path(args[2]), &cancel, std::nullopt, {}, nullptr, hook));
        } else if (command == "run-native" && args.size() == 7) {
            // run-native <store> <observer|hunt> <id> <probe|-> <policy> <cancel-after-ms|-> <experimental 0|1>; stdin {"engine", "digest"}
            const Value in = compat::parse(input());
            std::atomic<bool> cancel{false};
            std::thread timer;
            if (args[5] != "-") timer = std::thread([&cancel, delay = std::stoi(args[5])] {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                cancel.store(true);
            });
            struct Join { std::thread& t; ~Join() { if (t.joinable()) t.join(); } } join{timer};
            ok(session_runner::run_native(args[1] == "observer" ? std::optional<native_session::Adapter>(native_session::Adapter::observer) : std::nullopt,
                                          args[1] == "hunt", Store(fs::u8path(args[0])), wide(args[2]),
                                          content_internal::native_units(in.at(U"engine").string), in.at(U"digest"),
                                          args[6] == "1", optional_path(args[3]), &cancel, policies(args[4])));
        } else if (command == "reconcile" && args.size() == 4) {
            ok(reconciliation::reconcile_session(Store(fs::u8path(args[0])), wide(args[1]), optional_path(args[2]), policies(args[3])));
        } else if (command == "recover" && args.size() == 4) {
            ok(session_runner::recover_session(Store(fs::u8path(args[0])), wide(args[1]), optional_path(args[2]), policies(args[3])));
        } else return 2;
    } catch (const session_runner::Interrupted& e) { std::cout << "interrupted " << e.what() << '\n';
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
