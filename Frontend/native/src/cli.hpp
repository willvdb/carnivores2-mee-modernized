#pragma once
// PRIVATE native frontend command dispatcher: the complete frontend.py command
// surface over the native library. Production main() passes default seams; only
// the test-only fixture executable installs a policy double.
#include "session_policy.hpp"
#include <atomic>
#include <filesystem>
#include <ostream>
#include <vector>
namespace c2::frontend::cli {
struct Seams {
    session_policy::Policies policies;
};
// Set asynchronously by the SIGINT / console-control handler. A running owned
// session treats it as cancellation (the reference KeyboardInterrupt path);
// every other command runs its bounded operation to completion.
std::atomic<bool>& interrupt_flag() noexcept;
// Returns the process exit status: 0 success, 2 domain/usage error (JSON
// envelope on err), 3 resource exhaustion.
int run(const std::vector<std::filesystem::path>& args, std::ostream& out, std::ostream& err,
        const Seams& seams = {});
}
