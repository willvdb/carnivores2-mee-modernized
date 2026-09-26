// TEMPORARY integration stubs for workstream symbols not yet delivered to the
// integration branch. Each refuses explicitly (no side effect) so already-wired
// commands can be exercised. Every definition here must be deleted when the
// real implementation is integrated (a duplicate definition fails the link);
// the completion gate requires this file to be gone.
#include "acceptance.hpp"
namespace {
[[noreturn]] void pending(const char* what) {
    throw c2::frontend::StoreError(std::string("native operation not yet integrated: ") + what);
}
}
namespace c2::frontend {
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
