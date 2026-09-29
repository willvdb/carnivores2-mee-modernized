#pragma once
// Backend observations for the GUI, read through the real frontend core.
// No SDL, OpenGL or RmlUi types. Strings are UTF-8 for presentation; the
// identities are the backend's stable UUID strings, never display names.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace c2::frontend::gui {
struct HunterRow {
    std::string id;
    std::string name;
    bool archived = false;
};
struct ExpeditionRow {
    std::string id;
    std::string mode;
    std::string path_flavor;
    std::string path;
};
struct StoreSnapshot {
    std::string directory;
    // False when the directory has no lodge.json: the core then observes the
    // empty default manifest without creating anything. Shown, not hidden.
    bool manifest_present = true;
    int schema_version = 0;
    std::optional<std::string> active_hunter;
    std::vector<HunterRow> hunters;
    std::vector<ExpeditionRow> expeditions;
};
// Exactly one of snapshot/error is populated. A failed read is reported as an
// error string; it is never replaced by fabricated data.
struct SnapshotResult {
    std::optional<StoreSnapshot> snapshot;
    std::string error;
    bool ok() const noexcept { return snapshot.has_value(); }
};
// Reads `directory/lodge.json` with the core's Store::read(). Read-only: the
// core never creates, upgrades or repairs a store on read. A directory that
// does not exist is an error; an existing directory without a manifest is
// the core's empty default manifest with manifest_present = false.
SnapshotResult read_store_snapshot(const std::filesystem::path& directory);

// Presentation label for an expedition: the last path component of its
// installation locator, honoring the recorded path flavor. Falls back to the
// full path when there is no component.
std::string expedition_label(const ExpeditionRow& row);

// Strict UTF-8 conversions. Invalid scalar values (surrogate code points,
// values above U+10FFFF) become U+FFFD so corruption is visible, not silent.
std::string to_utf8(const std::u32string& text);
std::u32string to_utf32(const std::string& utf8);
} // namespace c2::frontend::gui
