#pragma once
// Authored, disposable evaluation store. The content is fixture metadata for
// the GUI demo and its tests; it is not a personal lodge and contains no game
// content. Writes go only into the uniquely owned directory created here.
#include <filesystem>
#include <string>
#include <vector>

namespace c2::frontend::gui {
struct DemoStoreIdentities {
    // Stable authored UUIDs, in manifest insertion order.
    std::vector<std::string> hunter_ids;
    std::vector<std::string> expedition_ids;
    std::string active_hunter;
};
// The authored manifest bytes (schema 1, raw UTF-8, LF line endings).
std::string demo_manifest_bytes();
const DemoStoreIdentities& demo_identities();

// Creates `parent/c2-frontend-gui-demo-<unique>` and writes lodge.json there.
// Never touches an existing directory: a collision is retried with a new name.
// Throws std::runtime_error if the directory cannot be created.
std::filesystem::path create_demo_store(const std::filesystem::path& parent);
// Convenience: parent = std::filesystem::temp_directory_path().
std::filesystem::path create_demo_store();
// Removes a directory previously returned by create_demo_store. Refuses any
// other path (returns false) so a mistaken call cannot delete user data.
bool remove_demo_store(const std::filesystem::path& directory);
} // namespace c2::frontend::gui
