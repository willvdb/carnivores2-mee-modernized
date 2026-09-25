#pragma once
// PRIVATE session preparation (workstream A; lodge.sessions + lodge.native_session
// + lodge.native_observer / native_hunt / native_continuation adapters).
// Values are the reference dictionaries as compat::Value, never a second schema.
// Errors: StoreError carrying reference FrontendError messages; OSError paths
// as std::filesystem::filesystem_error; TypeError paths as std::invalid_argument.
#include "planning_store.hpp"
#include "session_policy.hpp"
#include <atomic>
namespace c2::frontend::sessions {
using compat::Value;
inline constexpr std::u32string_view SYNTHETIC_POLICY = U"frontend-synthetic-observer-v1";
inline constexpr std::string_view LITERAL_ARGUMENT = "literal ; $(never-a-shell) & spaces";
// SCENARIOS in reference order.
const std::vector<std::u32string>& scenarios();
// Compiled synthetic child (replaces sys.executable + synthetic_child.py):
// c2-frontend-synthetic-child next to the running executable. Documented
// fixture/executable evidence difference; never read from a journal.
std::filesystem::path synthetic_fixture();
// sessions.execution_spec for the compiled fixture (same keys and order).
Value execution_spec(const std::filesystem::path& root, const Value& scenario, const Value& slot, const Value& timeout);
// sessions.prepare_session (schema-1 synthetic journal).
Value prepare_session(const Store&, std::u32string_view association_id, std::u32string area,
                      std::u32string scenario, const Value& time_of_day, const Value& timeout,
                      const std::optional<std::filesystem::path>& probe);
} // namespace c2::frontend::sessions

namespace c2::frontend::native_session {
using compat::Value;
// Journal schema 2 = observer, 3 = original-import hunt, 4 = managed continuation.
enum class Adapter { observer = 2, hunt = 3, continuation = 4 };
std::u32string_view kind(Adapter);      // KIND
std::u32string_view lifecycle(Adapter); // LIFECYCLE capability key
const Value& capability();              // CAPABILITY
const std::string& config();            // CONFIG bytes
bool supported_contract(const Value&);
// trusted_engine: experimental gate, digest format, executable_evidence, safe_path, hash equality.
Value trusted_engine(const std::filesystem::path& engine, const Value& digest, bool experimental);
// query_contract: only after explicit trust; runs `<engine> --session-capabilities`.
Value query_contract(const Value& evidence);
// workspace_findings as a list of diagnostic objects (never authorizes state access).
Value workspace_findings(const std::filesystem::path& root, bool returning);
// Adapter native_pins (observer: snapshot_pins observer; hunt: mode hunt;
// continuation: managed=true with optional explicit generation).
planning_store::PinSnapshot native_pins(Adapter, const Store&, std::u32string_view association_id,
    const Value& selection, const std::optional<std::filesystem::path>& probe,
    const std::optional<Value>& expected_codec, const session_policy::Policies&,
    const std::optional<std::u32string>& generation = std::nullopt);
// native_continuation.return_pins: compares against the actually pinned generation.
planning_store::PinSnapshot return_pins(const Store&, const Value& pins,
    const std::optional<std::filesystem::path>& probe, const session_policy::Policies&);
// Adapter execution_spec (shared.execution_spec with the adapter kind/policy).
Value execution_spec(Adapter, const std::filesystem::path& root, const Value& pins, const Value& evidence,
                     const Value& capability, const Value& timeout);
// adapter_for(journal): schema/kind agreement, else the reference refusal.
Adapter adapter_for(const Value& journal);
// native_hunt.preparation_adapter: continuation when the store is schema 2.
Adapter preparation_adapter(const Store&);
// shared.prepare for the given adapter.
Value prepare(Adapter, const Store&, std::u32string_view association_id, const Value& selection,
              const std::filesystem::path& engine, const Value& digest, bool experimental,
              const Value& timeout, const std::optional<std::filesystem::path>& probe,
              const session_policy::Policies&);
struct Authorization { std::filesystem::path engine; Value digest; bool experimental = false; };
} // namespace c2::frontend::native_session
