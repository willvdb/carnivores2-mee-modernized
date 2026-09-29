#include "resources.hpp"
#include <SDL3/SDL.h>
#include <cstdlib>

#ifndef C2_FRONTEND_GUI_SOURCE_ASSETS
#define C2_FRONTEND_GUI_SOURCE_ASSETS ""
#endif

namespace c2::frontend::gui::app {
namespace {
bool usable(const std::filesystem::path& root) {
    std::error_code ec;
    return std::filesystem::is_regular_file(root / "rml" / "lodge.rml", ec);
}
} // namespace

std::optional<AssetRoot> resolve_asset_root(const std::optional<std::filesystem::path>& override_dir,
                                            std::vector<std::string>& tried) {
    std::vector<AssetRoot> candidates;
    if (override_dir) candidates.push_back({*override_dir, "--assets"});
    if (const char* env = std::getenv("C2_FRONTEND_GUI_ASSETS"); env && *env)
        candidates.push_back({std::filesystem::u8path(env), "C2_FRONTEND_GUI_ASSETS"});
    if (const char* base = SDL_GetBasePath(); base && *base) {
        const auto exe_dir = std::filesystem::u8path(base);
        candidates.push_back({exe_dir / ".." / "share" / "c2-frontend-gui", "install layout next to the executable"});
    }
    const std::string source = C2_FRONTEND_GUI_SOURCE_ASSETS;
    if (!source.empty()) candidates.push_back({std::filesystem::u8path(source), "build-time source assets (development fallback)"});
    for (auto& c : candidates) {
        std::error_code ec;
        auto normal = std::filesystem::weakly_canonical(c.directory, ec);
        if (ec) normal = c.directory.lexically_normal();
        tried.push_back(normal.u8string() + " [" + c.origin + "]");
        if (usable(normal)) return AssetRoot{normal, c.origin};
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> missing_essentials(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> missing;
    for (const char* rel : {"rml/lodge.rml", "rml/console.rml", "rml/setup.rml", "rml/preview.rml",
                            "rcss/theme.rcss", "fonts/DejaVuSans.ttf"}) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(root / rel, ec)) missing.push_back(root / rel);
    }
    return missing;
}
} // namespace c2::frontend::gui::app
