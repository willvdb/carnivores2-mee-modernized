#pragma once

#include "RefreshSelection.h"
#include "MonitorPreference.h"

namespace GameDisplay {

enum class DisplayFallback { None, IndexOutOfRange, MissingBounds, MissingPrimary, AmbiguousBounds, MissingIdentity, AmbiguousIdentity, UnsupportedIdentity };
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
                std::size_t matches = 0;
                for (const auto& display : catalog.displays) {
                    if (display.bounds && Platform::EqualDisplayBounds(*display.bounds, *bounds) && ++matches > 1) {
                        result.fallback = DisplayFallback::AmbiguousBounds;
                        // Keep primary/default, with neither a target nor an
                        // explicit mode. Do not select primary's refresh below.
                        return result;
                    }
                }
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

// Persistent identity is resolved afresh on every application. Failed identity
// resolution uses primary/default AND automatic, without changing saved intent.
inline DisplaySelection SelectMonitor(const Platform::DisplayCatalog& catalog,
                                      const MonitorPreference& preference,
                                      Platform::Size size, Platform::RefreshRate refresh = {})
{
    if (preference.kind == MonitorPreferenceKind::Primary) return SelectDisplay(catalog, std::nullopt, size, refresh);
    if (preference.kind == MonitorPreferenceKind::SessionIndex) return SelectDisplay(catalog, preference.index, size, refresh);
    auto fallback = SelectDisplay(catalog, std::nullopt, size);
    fallback.fallback = DisplayFallback::MissingIdentity;
    if (preference.identity.version != 1 || (preference.identity.domain != "win-monitor-interface" &&
        preference.identity.domain != "linux-x11-edid-serial" &&
        preference.identity.domain != "linux-wayland-wlr-serial")) {
        fallback.fallback = DisplayFallback::UnsupportedIdentity;
        return fallback;
    }
    std::optional<std::uint32_t> match;
    for (std::size_t i = 0; i < catalog.displays.size(); ++i) {
        const auto& identity = catalog.displays[i].identity;
        if (identity && Platform::EqualDisplayIdentity(*identity, preference.identity)) {
            if (match || i > (std::numeric_limits<std::uint32_t>::max)()) {
                fallback.fallback = DisplayFallback::AmbiguousIdentity;
                return fallback;
            }
            match = static_cast<std::uint32_t>(i);
        }
    }
    if (!match) return fallback;
    auto selected = SelectDisplay(catalog, match, size, refresh);
    if (selected.fallback != DisplayFallback::None) selected.exclusiveMode.reset();
    return selected;
}

inline const char* DisplayFallbackReason(DisplayFallback fallback)
{
    switch (fallback) {
    case DisplayFallback::IndexOutOfRange: return "index outside current catalog";
    case DisplayFallback::MissingBounds: return "display has no usable bounds";
    case DisplayFallback::MissingPrimary: return "catalog has no primary display";
    case DisplayFallback::AmbiguousBounds: return "display bounds match multiple displays";
    case DisplayFallback::MissingIdentity: return "saved identity unavailable on this backend/topology";
    case DisplayFallback::AmbiguousIdentity: return "saved identity matches multiple displays";
    case DisplayFallback::UnsupportedIdentity: return "unsupported identity version/domain";
    default: return "none";
    }
}

} // namespace GameDisplay
