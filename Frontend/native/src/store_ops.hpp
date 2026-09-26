#pragma once
// PRIVATE manifest mutations and imports (workstream B, 5A/5B). Functions that
// take `Value& data` edit a transaction's retained manifest (the CLI wraps them
// in store_write::transaction exactly where frontend.py uses store.transaction()).
#include "c2/frontend/store.hpp"
#include "json_compat.hpp"
#include "store_write.hpp"
namespace c2::frontend::store_ops {
using compat::Value;
// lodge.store.hunter (create/select/rename/archive).
Value hunter(Value& data, std::u32string_view action, const std::optional<std::u32string>& identity,
             const std::optional<std::u32string>& name);
// frontend.py host-settings --json: parse, validate object keyed by display/audio/input, update.
Value update_host_settings(Value& data, std::string_view json_text);
// Store.restore_backup (recover-backup).
void restore_backup(const Store&, const store_write::FailureHook& hook = {});
// lodge.discovery.register / relocate / refresh_instance(get_instance(...)).
Value register_instance(Value& data, const std::filesystem::path& path, std::u32string_view mode,
                        std::u32string_view dialect, const std::optional<std::u32string>& family,
                        const std::optional<std::u32string>& release,
                        const std::optional<std::filesystem::path>& managed_root);
Value relocate(Value& data, std::u32string_view identity, const std::filesystem::path& path);
Value refresh_instance(Value& data, std::u32string_view identity);
// expedition discover (read-only view with possible_moves) and --register-managed.
Value discover_view(const Manifest&, const std::filesystem::path& directory);
Value discover_register(Value& data, const std::filesystem::path& directory);
// lodge.profiles.associate (including explicit lossless --import-copy).
Value associate(const Store&, Value& data, std::u32string_view hunter_id, std::u32string_view instance_id,
                std::u32string_view state_key, std::u32string_view origin,
                const std::optional<std::u32string>& ownership, const std::optional<std::filesystem::path>& probe,
                const store_write::FailureHook& hook = {});
// lodge.managed_state.upgrade_store (explicit schema 1 -> 2).
Value upgrade_store(const Store&, const store_write::FailureHook& hook = {});
}
