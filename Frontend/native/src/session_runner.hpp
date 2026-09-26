#pragma once
// PRIVATE lodge.session_runner + lodge.reconciliation (workstream A, 4B/4C).
#include "sessions.hpp"
#include "session_process.hpp"
namespace c2::frontend::session_runner {
using compat::Value;
// The KeyboardInterrupt analogue: raised when `interrupt` is set before the
// child was spawned. Before the launching write the journal remains
// prepared; after it (but before the spawn) the journal is left launching
// and recovery marks it interrupted. Once the child runs, an interrupt
// behaves as cancellation, exactly as the reference's SIGINT path does.
class Interrupted : public std::runtime_error { public: using std::runtime_error::runtime_error; };
// run_session: preflight by journal kind, durable launching/running/returned
// transitions, failed on preflight/spawn failure. native_authorization is
// required for schema >= 2. cancel may be null; a set cancel still launches
// the child and then stops it (the reference Event semantics). interrupt may
// be null (the CLI passes its SIGINT flag here, never as cancel). The
// test-only hook reaches every journal write of the run (see store_write).
Value run_session(const Store&, std::u32string_view identity, const std::optional<std::filesystem::path>& probe,
                  const std::atomic<bool>* cancel, const std::optional<native_session::Authorization>&,
                  const session_policy::Policies&, const std::atomic<bool>* interrupt = nullptr,
                  const store_write::FailureHook& hook = {});
// native_session.run: journal adapter must equal `expected` (observer) or, for
// native-hunt run, be hunt/continuation (pass std::nullopt with hunt_run=true).
Value run_native(std::optional<native_session::Adapter> expected, bool hunt_run, const Store&,
                 std::u32string_view identity, const std::filesystem::path& engine, const Value& digest,
                 bool experimental, const std::optional<std::filesystem::path>& probe,
                 const std::atomic<bool>* cancel, const session_policy::Policies&,
                 const std::atomic<bool>* interrupt = nullptr);
// recover_session: never signals a journal PID, never relaunches.
Value recover_session(const Store&, std::u32string_view identity, const std::optional<std::filesystem::path>& probe,
                      const session_policy::Policies&);
}
namespace c2::frontend::reconciliation {
using compat::Value;
Value reconcile_locked(const Store&, const std::filesystem::path& root, Value& journal,
                       const std::optional<std::filesystem::path>& probe, const session_policy::Policies&);
Value reconcile_session(const Store&, std::u32string_view identity, const std::optional<std::filesystem::path>& probe,
                        const session_policy::Policies&);
}
