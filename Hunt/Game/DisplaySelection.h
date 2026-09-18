#pragma once

#include "RefreshSelection.h"

namespace GameDisplay {

enum class DisplayFallback { None, IndexOutOfRange, MissingBounds, MissingPrimary };
struct DisplaySelection {
    // Only used against the same snapshot in engine policy/diagnostics.
    std::optional<Platform::DisplayIndex> index;
    std::optional<Platform::DisplayTarget> target;
    std::optional<Platform::DisplayMode> exclusiveMode;
    DisplayFallback fallback = DisplayFallback::None;
};

// Resolve presentation and refresh together against ONE owned snapshot. The
// resulting bounds/mode own all data needed after that snapshot is destroyed.
inline DisplaySelection SelectDisplay(const Platform::DisplayCatalog& catalog,
                                      std::optional<std::uint32_t> requested,
                                      Platform::Size size, Platform::RefreshRate refresh = {})
{
    DisplaySelection result;
    if (catalog.primaryDisplay && *catalog.primaryDisplay < catalog.displays.size())
        result.index = catalog.primaryDisplay;
    if (requested) {
        if (*requested >= catalog.displays.size()) {
            result.fallback = DisplayFallback::IndexOutOfRange;
        } else {
            const auto& bounds = catalog.displays[*requested].bounds;
            if (!bounds || bounds->size.width <= 0 || bounds->size.height <= 0) {
                result.fallback = DisplayFallback::MissingBounds;
            } else {
                result.index = *requested;
                result.target = Platform::DisplayTarget{*bounds};
            }
        }
    }
    if (!result.index) {
        if (result.fallback == DisplayFallback::None)
            result.fallback = DisplayFallback::MissingPrimary;
        return result; // Keep the backend's established default-device fallback.
    }
    const auto& modes = catalog.displays[*result.index].modes;
    const auto selected = FindRefreshMode(modes, size, refresh);
    if (selected) result.exclusiveMode = modes[*selected];
    return result;
}

inline const char* DisplayFallbackReason(DisplayFallback fallback)
{
    switch (fallback) {
    case DisplayFallback::IndexOutOfRange: return "index outside current catalog";
    case DisplayFallback::MissingBounds: return "display has no usable bounds";
    case DisplayFallback::MissingPrimary: return "catalog has no primary display";
    default: return "none";
    }
}

} // namespace GameDisplay
