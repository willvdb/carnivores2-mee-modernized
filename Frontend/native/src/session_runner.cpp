// Workstream A: lodge.session_runner (preflight, run_session, recover_session)
// and lodge.native_session.run over the owned child supervisor. See session_runner.hpp.
#include "session_runner.hpp"
#include "c2/frontend/catalog.hpp"
#include "c2/frontend/content.hpp"
#include "capture.hpp"
#include "content_internal.hpp"
#include "planning_internal.hpp"
#include "probe_process.hpp"
#include "schema_compat.hpp"
#include "session_journal.hpp"
#include "store_paths.hpp"
#include "store_write.hpp"
#include <algorithm>
#include <thread>
namespace fs = std::filesystem;
namespace c2::frontend::session_runner {
namespace {
using compat::Kind;
using compat::Value;
using namespace planning_internal;
const std::u32string& text(const Value& object, std::u32string_view key) {
    const Value& v = object.at(key);
    if (v.kind != Kind::string) throw std::invalid_argument("retained field is not a string");
    return v.string;
}
Value get(const Value& object, std::u32string_view key) { return object.contains(key) ? object.at(key) : Value{}; }
Value* member(Value& object, std::u32string_view key) {
    if (object.kind != Kind::object) return nullptr;
    for (auto& m : object.object) if (m.first == key) return &m.second;
    return nullptr;
}
void assign(Value& object, std::u32string_view key, Value value) {
    if (Value* existing = member(object, key)) { *existing = std::move(value); return; }
    object.object.emplace_back(std::u32string(key), std::move(value));
}
fs::path path_of(const Value& value) {
    if (value.kind != Kind::string) throw std::invalid_argument("retained path is not a string");
    return content_internal::native_units(value.string);
}
int schema_of(const Value& journal) {
    const Value& v = journal.at(U"schema_version");
    return v.kind == Kind::integer && v.integer.size() == 1 ? v.integer[0] - '0' : 0;
}
bool is_false(const Value& v) { return v.kind == Kind::boolean && !v.boolean; }
Value diagnostic(std::string_view code, const std::string& message) {
    Value v = object_value();
    v.object.emplace_back(U"code", ascii_value(code));
    v.object.emplace_back(U"message", string_value(store_paths::native_points(fs::u8path(message))));
    return v;
}
std::vector<std::u32string> entry_names(const fs::path& directory) {
    std::vector<std::u32string> names;
    for (const auto& entry : fs::directory_iterator(directory)) names.push_back(store_paths::native_points(entry.path().filename()));
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}
bool has_entries(const fs::path& directory) { return fs::directory_iterator(directory) != fs::directory_iterator(); }
// Code points as UTF-8 for a refusal message (lone surrogates are escaped bytes on POSIX).
std::string narrow(const std::u32string& points) { return content_internal::native_units(points).u8string(); }
// The reference's `except (FrontendError, OSError, KeyError, TypeError)`;
// FrontendError-derived messages are exact, the others are native texts.
template <class F> std::optional<std::string> guarded(F&& f) {
    try { f(); return std::nullopt; }
    catch (const ResourceExhausted&) { throw; }
    catch (const StoreError& e) { return e.what(); }
    catch (const probe_process::ProbeError& e) { return e.what(); }
    catch (const planning::Error& e) { return e.what(); }
    catch (const catalog::Error& e) { return e.what(); }
    catch (const ContentError& e) { return e.what(); }
    catch (const fs::filesystem_error& e) { return e.what(); }
    catch (const std::invalid_argument& e) { return e.what(); }
    catch (const compat::Error& e) { return e.what(); }
}
// session_runner.preflight: the fixed synthetic fixture only.
void synthetic_preflight(const Store& store, const fs::path& root, const Value& journal, const std::optional<fs::path>& probe) {
    const Value& pins = journal.at(U"pins");
    const auto current = planning_store::snapshot_pins(store, text(pins, U"association_id"), pins.at(U"selection"), probe, pins.at(U"codec"));
    if (!schema::equal(current.pins, pins))
        throw StoreError("pinned identity, content, engine, policy, codec or source evidence changed");
    const Value& spec = journal.at(U"execution");
    const Value expected = sessions::execution_spec(root, get(spec, U"scenario"), pins.at(U"native_slot"), get(spec, U"timeout_seconds"));
    if (!schema::equal(spec, expected))
        throw StoreError("execution evidence changed or command is not the fixed synthetic fixture");
    for (const fs::path& directory : {root / "baseline", root / "work" / "state"}) {
        const Value entries = capture(directory).entry_value();
        if (!schema::equal(entries, journal.at(U"baseline_members")) || !schema::equal(entries, pins.at(U"source_members")))
            throw StoreError("session baseline/workspace changed before launch");
    }
    store_paths::safe_path(root / "work");
    if (entry_names(root / "work") != std::vector<std::u32string>{U"state"}) throw StoreError("unexpected workspace entry before launch");
    store_paths::safe_path(root / "logs");
    if (has_entries(root / "logs")) throw StoreError("session logs already exist; no relaunch");
}
// native_session.preflight for the journal's adapter.
void native_preflight(const Store& store, const fs::path& root, const Value& journal, const std::optional<fs::path>& probe,
                      const std::optional<native_session::Authorization>& authorization, native_session::Adapter adapter,
                      const session_policy::Policies& policies) {
    if (!authorization) throw StoreError("native sessions require the separate explicitly trusted developer run command");
    const Value evidence = native_session::trusted_engine(authorization->engine, authorization->digest, authorization->experimental);
    const Value& spec = journal.at(U"execution");
    const Value& pins = journal.at(U"pins");
    if (!schema::equal(evidence, spec.at(U"executable"))) throw StoreError("selected engine differs from the prepared trusted engine");
    const auto current = native_session::native_pins(adapter, store, text(pins, U"association_id"), pins.at(U"selection"), probe,
                                                     pins.at(U"codec"), policies);
    if (!schema::equal(current.pins, pins)) throw StoreError("native identity/content/policy/source pins changed");
    // Re-query only the same explicitly trusted, unchanged binary.
    const Value contract = native_session::query_contract(evidence);
    const Value expected = native_session::execution_spec(adapter, root, pins, evidence, contract, get(spec, U"timeout_seconds"));
    if (!schema::equal(expected, spec) || !native_session::supported_contract(get(spec, U"contract")) || !is_false(get(spec, U"shell")))
        throw StoreError("native execution specification changed");
    for (const fs::path& directory : {root / "baseline", root / "work" / "state"}) {
        const Value entries = capture(directory).entry_value();
        if (!schema::equal(entries, journal.at(U"baseline_members")) || !schema::equal(entries, pins.at(U"source_members")))
            throw StoreError("native baseline/work state changed before launch");
    }
    // validate_workspace: every finding message joined.
    const Value findings = native_session::workspace_findings(root, false);
    if (!findings.array.empty()) {
        std::u32string joined;
        for (const Value& finding : findings.array) {
            if (!joined.empty()) joined += U"; ";
            joined += text(finding, U"message");
        }
        throw StoreError(narrow(joined));
    }
    store_paths::safe_path(root / "logs");
    if (has_entries(root / "logs")) throw StoreError("session logs already exist; no relaunch");
}
// After the spawn an interrupt is a cancellation. Both flags are watched when
// a caller supplies both; the supervisor polls a single flag.
struct StopFlags {
    std::atomic<bool> either{false}, done{false};
    std::thread poller;
    const std::atomic<bool>* flag = nullptr;
    StopFlags(const std::atomic<bool>* cancel, const std::atomic<bool>* interrupt) {
        if (cancel && interrupt) {
            flag = &either;
            poller = std::thread([this, cancel, interrupt] {
                while (!done.load()) {
                    if (cancel->load() || interrupt->load()) { either.store(true); return; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            });
        } else flag = cancel ? cancel : interrupt;
    }
    ~StopFlags() { done.store(true); if (poller.joinable()) poller.join(); }
};
bool set(const std::atomic<bool>* flag) { return flag && flag->load(); }
double seconds(const Value& timeout) {
    if (timeout.kind == Kind::floating) return timeout.floating;
    if (timeout.kind == Kind::integer) return std::stod(timeout.integer);
    throw std::invalid_argument("timeout is not a number");
}
} // namespace

Value run_session(const Store& store, std::u32string_view identity, const std::optional<fs::path>& probe,
                  const std::atomic<bool>* cancel, const std::optional<native_session::Authorization>& authorization,
                  const session_policy::Policies& policies, const std::atomic<bool>* interrupt,
                  const store_write::FailureHook& hook) {
    // Launch once after the journal kind's preflight; never accept raw argv.
    store_write::WriterLock lock(store.directory());
    const fs::path root = store_write::session_root(store, identity);
    if (set(interrupt)) throw Interrupted("interrupted before launch; session remains prepared");
    Value journal = session_journal::read(store, identity);
    const int schema = schema_of(journal);
    if (!is_text(journal.at(U"state"), U"prepared")) throw StoreError("only a prepared session can launch; recovery never relaunches");
    if (schema >= 2 && !authorization) throw StoreError("native observer requires the separate gated developer run command");
    if (const auto failure = guarded([&] {
        if (schema >= 2) native_preflight(store, root, journal, probe, authorization, native_session::adapter_for(journal), policies);
        else synthetic_preflight(store, root, journal, probe);
    })) {
        // A terminal Ctrl-C reaches the whole foreground process group, so it can be what
        // failed the preflight (a killed capability query or codec probe). The reference's
        // KeyboardInterrupt escapes preflight without a journal write; so does the interrupt.
        if (set(interrupt)) throw Interrupted("interrupted before launch; session remains prepared");
        member(journal, U"diagnostics")->array.push_back(diagnostic("preflight-failed", *failure));
        session_journal::transition(root, journal, "failed", {{U"failed_at", ascii_value(store_write::now())}}, hook);
        lock.release();
        return journal;
    }
    const fs::path stdout_log = root / "logs" / "stdout.log", stderr_log = root / "logs" / "stderr.log";
    store_paths::safe_path(stdout_log);
    store_paths::safe_path(stderr_log);
    if (set(interrupt)) throw Interrupted("interrupted before launch; session remains prepared");
    session_journal::transition(root, journal, "launching", {{U"launch_intent_at", ascii_value(store_write::now())}}, hook);
    if (set(interrupt)) throw Interrupted("interrupted before spawn; session left launching, recover marks it interrupted");
    const Value spec = journal.at(U"execution");
    session_process::Spec process_spec;
    process_spec.executable = path_of(spec.at(U"executable").at(U"path"));
    for (const Value& argument : spec.at(U"argv").array) process_spec.argv.push_back(path_of(argument));
    process_spec.cwd = path_of(spec.at(U"cwd"));
    process_spec.timeout = std::chrono::duration<double>(seconds(spec.at(U"timeout_seconds")));
    session_process::Outcome outcome;
    bool started = false;
    const StopFlags stop(cancel, interrupt);
    try {
        outcome = session_process::run(process_spec, stdout_log, stderr_log, stop.flag, [&](long long pid, const std::string& started_at) {
            started = true;
            Value process = object_value();
            process.object = {{U"pid", integer_value(std::to_string(pid))}, {U"started_at", ascii_value(started_at)}, {U"exit_code", Value{}}};
            session_journal::transition(root, journal, "running", {{U"launched_at", ascii_value(started_at)}, {U"process", std::move(process)}}, hook);
        });
    } catch (const fs::filesystem_error& error) {
        // Popen's OSError: the child never started, no log exists. A failure
        // after the start (the running-journal write) propagates as it does in
        // the reference, after the supervisor stopped the owned child.
        if (started) throw;
        member(journal, U"diagnostics")->array.push_back(diagnostic("spawn-failed", error.what()));
        session_journal::transition(root, journal, "failed", {{U"failed_at", ascii_value(store_write::now())}}, hook);
        lock.release();
        return journal;
    }
    const std::string reason(session_process::reason_text(outcome.reason));
    Value logs = object_value();
    logs.object = {{U"stdout", outcome.out.value()}, {U"stderr", outcome.err.value()}};
    if (outcome.exit_code != 0 || reason != "exited") {
        Value d = object_value();
        d.object = {{U"code", ascii_value("process-" + reason)}, {U"exit_code", integer_value(std::to_string(outcome.exit_code))}};
        member(journal, U"diagnostics")->array.push_back(std::move(d));
    }
    auto failed = [](const session_process::LogResult& log) { return log.error && !log.error->empty(); };
    if (failed(outcome.out) || failed(outcome.err)) {
        Value d = object_value();
        d.object.emplace_back(U"code", ascii_value("log-capture-failed"));
        member(journal, U"diagnostics")->array.push_back(std::move(d));
    }
    Value& capabilities = *member(journal, U"capabilities");
    if (schema >= 2) {
        assign(capabilities, native_session::lifecycle(native_session::adapter_for(journal)), ascii_value("completed"));
        assign(capabilities, U"engine_process_executed", boolean_value(true));
        // Process completion alone cannot certify entry into the game world.
    } else {
        assign(capabilities, U"synthetic_child_lifecycle", ascii_value("completed"));
    }
    Value process = object_value();
    process.object = {{U"pid", integer_value(std::to_string(outcome.pid))}, {U"started_at", ascii_value(outcome.started_at)},
        {U"returned_at", ascii_value(outcome.returned_at)}, {U"exit_code", integer_value(std::to_string(outcome.exit_code))},
        {U"stop_reason", ascii_value(reason)}};
    session_journal::transition(root, journal, "returned", {{U"returned_at", ascii_value(outcome.returned_at)},
        {U"logs", std::move(logs)}, {U"process", std::move(process)}}, hook);
    lock.release();
    return journal;
}

Value run_native(std::optional<native_session::Adapter> expected, bool hunt_run, const Store& store,
                 std::u32string_view identity, const fs::path& engine, const Value& digest, bool experimental,
                 const std::optional<fs::path>& probe, const std::atomic<bool>* cancel, const session_policy::Policies& policies,
                 const std::atomic<bool>* interrupt) {
    native_session::Adapter adapter;
    if (hunt_run) {
        // native_hunt.run_hunt selects the journal's adapter, then shared.run rereads.
        adapter = native_session::adapter_for(session_journal::read(store, identity));
        if (adapter == native_session::Adapter::observer) throw StoreError("native-hunt run requires a normal-hunt journal");
    } else if (expected) {
        adapter = *expected;
    } else {
        throw std::invalid_argument("run_native requires the expected adapter unless hunt_run is set");
    }
    const Value journal = session_journal::read(store, identity);
    if (native_session::adapter_for(journal) != adapter) throw StoreError("native command does not match the prepared session kind");
    native_session::trusted_engine(engine, digest, experimental);
    return run_session(store, identity, probe, cancel, native_session::Authorization{engine, digest, experimental}, policies, interrupt);
}

Value recover_session(const Store& store, std::u32string_view identity, const std::optional<fs::path>& probe,
                      const session_policy::Policies& policies) {
    // Never infer child death from a PID or read potentially live returned state.
    store_write::WriterLock lock(store.directory());
    const fs::path root = store_write::session_root(store, identity);
    Value journal = session_journal::read(store, identity);
    const std::u32string state = text(journal, U"state");
    if (state == U"launching" || state == U"running") {
        member(journal, U"diagnostics")->array.push_back(diagnostic("process-ownership-lost",
            "Child may still exist. No signal, relaunch or state capture performed."));
        session_journal::transition(root, journal, "interrupted", {{U"interrupted_at", ascii_value(store_write::now())}});
    } else if (state == U"returned" || state == U"inspecting") {
        Value result = reconciliation::reconcile_locked(store, root, journal, probe, policies);
        lock.release();
        return result;
    }
    lock.release();
    return journal;
}
} // namespace c2::frontend::session_runner
