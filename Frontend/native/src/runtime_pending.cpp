// TEMPORARY integration stubs for workstream symbols not yet delivered to the
// integration branch. Each refuses explicitly (no side effect) so already-wired
// commands can be exercised. Every definition here must be deleted when the
// real implementation is integrated (a duplicate definition fails the link);
// the completion gate requires this file to be gone.
#include "acceptance.hpp"
#include "session_runner.hpp"
#include "sessions.hpp"
namespace {
[[noreturn]] void pending(const char* what) {
    throw c2::frontend::StoreError(std::string("native operation not yet integrated: ") + what);
}
}
namespace c2::frontend {
namespace sessions {
const std::vector<std::u32string>& scenarios() {
    static const std::vector<std::u32string> list{U"unchanged", U"sav", U"pair", U"nonzero", U"changed-nonzero",
        U"corrupt-sav", U"corrupt-sab", U"missing-sab", U"deleted-sav", U"extra", U"registration", U"hang",
        U"terminated", U"logs"};
    return list;
}
Value prepare_session(const Store&, std::u32string_view, std::u32string, std::u32string, const Value&, const Value&,
                      const std::optional<std::filesystem::path>&) { pending("session prepare-synthetic"); }
}
namespace native_session {
Adapter preparation_adapter(const Store&) { pending("native-hunt prepare"); }
Value prepare(Adapter, const Store&, std::u32string_view, const Value&, const std::filesystem::path&, const Value&,
              bool, const Value&, const std::optional<std::filesystem::path>&, const session_policy::Policies&) {
    pending("native session prepare");
}
}
namespace session_runner {
Value run_session(const Store&, std::u32string_view, const std::optional<std::filesystem::path>&,
                  const std::atomic<bool>*, const std::optional<native_session::Authorization>&,
                  const session_policy::Policies&) { pending("session run"); }
Value run_native(std::optional<native_session::Adapter>, bool, const Store&, std::u32string_view,
                 const std::filesystem::path&, const Value&, bool, const std::optional<std::filesystem::path>&,
                 const std::atomic<bool>*, const session_policy::Policies&) { pending("native run"); }
Value recover_session(const Store&, std::u32string_view, const std::optional<std::filesystem::path>&,
                      const session_policy::Policies&) { pending("session recover"); }
}
namespace reconciliation {
Value reconcile_session(const Store&, std::u32string_view, const std::optional<std::filesystem::path>&,
                        const session_policy::Policies&) { pending("session reconcile"); }
}
namespace acceptance {
Value preview_acceptance(const Store&, std::u32string_view, std::u32string_view,
                         const std::optional<std::filesystem::path>&, const session_policy::Policies&) {
    pending("managed-state preview");
}
Value accept_candidate(const Store&, std::u32string_view, std::u32string_view, std::u32string_view,
                       const std::optional<std::filesystem::path>&, const session_policy::Policies&,
                       const store_write::FailureHook&) { pending("managed-state accept"); }
Value recover_acceptance(const Store&, std::u32string_view, const store_write::FailureHook&) {
    pending("managed-state recover-acceptance");
}
}
}
