#pragma once
// PRIVATE write primitives of lodge.store / lodge.session_io (slice 3A). No
// journal persistence, backup restoration, manifest mutations, process
// execution, CLI or public API. Reference: Store.lock, Store.transaction,
// atomic_write, now, new_id, session_root and write_blobs.
#include "c2/frontend/store.hpp"
#include "json_compat.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
namespace c2::frontend::store_write {
// Test-only failure injection. Production callers omit the hook; a hook
// throws at the named phase and can neither alter bytes nor skip a step.
enum class WritePhase {
    temp_create, temp_write, temp_fsync, replace, directory_fsync, lock_write, lock_fsync, blob_directory_fsync
};
// The path is the object acted on (temporary, target, directory or lock).
using FailureHook = std::function<void(WritePhase, const std::filesystem::path&)>;
// datetime.now(timezone.utc).isoformat(): microseconds omitted when zero.
// Seconds since the epoch within years 1..9999 on every platform; outside
// that range (or microseconds >= 1000000) throws StoreError.
std::string isoformat_utc(std::int64_t seconds, unsigned microseconds);
std::string now();
// str(uuid.uuid4()) from the operating-system CSPRNG.
std::string new_id();
// Same-volume replacement via a .pending- exclusive temporary next to the
// target; an interrupted write leaves the old file intact. OS failures throw
// std::filesystem::filesystem_error after the temporary is removed.
void atomic_write(const std::filesystem::path& path, std::string_view content, const FailureHook& hook = {});
// Store.lock: lodge.lock created exclusively under the store directory and
// removed on release. Never steals, ages out or removes a foreign lock.
class WriterLock {
public:
    explicit WriterLock(const std::filesystem::path& directory, const FailureHook& hook = {});
    ~WriterLock();
    WriterLock(const WriterLock&) = delete;
    WriterLock& operator=(const WriterLock&) = delete;
    // Explicit release reports unlink failure; the destructor cannot.
    void release();
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
    bool held_ = false;
};
// Store.transaction: read under the lock, let the caller edit the retained
// value, revalidate, and write (after copying the current bytes to
// lodge.json.bak) only when the manifest is absent or the sorted comparison
// changed. Returns whether the manifest was written. Callback and validation
// failures write nothing; the lock is released on every path.
bool transaction(const Store& store, const std::function<void(compat::Value&)>& mutate, const FailureHook& hook = {});
// managed_state.sync_directory: safe_path check, then fsync the directory on
// POSIX (no-op on Windows, as in the reference). OS failures throw
// std::filesystem::filesystem_error.
void sync_directory(const std::filesystem::path& directory);
// session_io.session_root: validated UUID below <store>/sessions, safe-path checked.
std::filesystem::path session_root(const Store& store, std::u32string_view identity);
// session_io.write_blobs over captured relative POSIX names.
void write_blobs(const std::filesystem::path& directory, const std::vector<CapturedBlob>& blobs, const FailureHook& hook = {});
}
