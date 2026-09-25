// TEMPORARY integration stubs for workstream symbols not yet delivered to the
// integration branch. Each refuses explicitly (no side effect) so already-wired
// commands can be exercised. Every definition here must be deleted when the
// real implementation is integrated (a duplicate definition fails the link);
// the completion gate requires this file to be gone.
#include "acceptance.hpp"
#include "session_runner.hpp"
#include "sessions.hpp"
#include "store_ops.hpp"
namespace {
[[noreturn]] void pending(const char* what) {
    throw c2::frontend::StoreError(std::string("native operation not yet integrated: ") + what);
}
}
namespace c2::frontend {
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
namespace store_ops {
Value hunter(Value&, std::u32string_view, const std::optional<std::u32string>&, const std::optional<std::u32string>&) {
    pending("hunter mutation");
}
Value update_host_settings(Value&, std::string_view) { pending("host-settings --json"); }
void restore_backup(const Store&, const store_write::FailureHook&) { pending("recover-backup"); }
Value register_instance(Value&, const std::filesystem::path&, std::u32string_view, std::u32string_view,
                        const std::optional<std::u32string>&, const std::optional<std::u32string>&,
                        const std::optional<std::filesystem::path>&) { pending("expedition register"); }
Value relocate(Value&, std::u32string_view, const std::filesystem::path&) { pending("expedition relocate"); }
Value refresh_instance(Value&, std::u32string_view) { pending("expedition refresh"); }
Value discover_view(const Manifest&, const std::filesystem::path&) { pending("expedition discover"); }
Value discover_register(Value&, const std::filesystem::path&) { pending("expedition discover --register-managed"); }
Value associate(const Store&, Value&, std::u32string_view, std::u32string_view, std::u32string_view, std::u32string_view,
                const std::optional<std::u32string>&, const std::optional<std::filesystem::path>&,
                const store_write::FailureHook&) { pending("associate"); }
Value upgrade_store(const Store&, const store_write::FailureHook&) { pending("managed-state upgrade"); }
}
}
