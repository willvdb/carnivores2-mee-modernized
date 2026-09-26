#pragma once
// PRIVATE POSIX test seam. Never set by production callers or environment.
#ifndef _WIN32
#include <memory>
#include <sys/types.h>
namespace c2::frontend::probe_process::testing {
enum class ReapEvent { signal, synchronous_wait, transfer, waiter_acquired, waiter_reaped };
struct ReapObserver {
    // Report the synchronous WNOHANG attempt as still pending through the real
    // grace period. The actual kill, ownership transfer and waiter stay intact.
    bool defer_synchronous_reap = false;
    virtual ~ReapObserver() = default;
    virtual void observe(ReapEvent event, pid_t pid, int status = 0) noexcept = 0;
};
// Captured before fork by Child and its waiter, so neither uses a test stack
// reference. Thread-local installation affects only this caller's next runs.
extern thread_local std::shared_ptr<ReapObserver> reap_observer;
}
#endif
