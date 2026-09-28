#pragma once
// Runtime resource resolution, independent of the working directory.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace c2::frontend::gui::app {
struct AssetRoot {
    std::filesystem::path directory;
    std::string origin;   // which rule selected it, for diagnostics
};
// Resolution order: explicit override (--assets), C2_FRONTEND_GUI_ASSETS,
// <executable dir>/../share/c2-frontend-gui (staged install layout), then the
// build-time source assets directory for uninstalled development binaries.
// Only the first candidate that contains rml/lodge.rml is accepted.
std::optional<AssetRoot> resolve_asset_root(const std::optional<std::filesystem::path>& override_dir,
                                            std::vector<std::string>& tried);
// Essential files (documents, stylesheet, font) that must exist for the GUI
// to start; missing ones are returned so the diagnostic names them.
std::vector<std::filesystem::path> missing_essentials(const std::filesystem::path& root);
} // namespace c2::frontend::gui::app
