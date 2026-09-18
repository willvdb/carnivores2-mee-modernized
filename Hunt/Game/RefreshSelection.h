#pragma once

#include "../Platform/Platform.h"

namespace GameDisplay {

inline bool MatchesRefreshMode(const Platform::DisplayMode& mode, Platform::Size size,
                               Platform::RefreshRate refresh)
{
    return mode.size.width == size.width && mode.size.height == size.height &&
        mode.bitsPerPixel >= 16 && Platform::EqualRefresh(mode.refresh, refresh);
}

// Inactive/unsupported requests both return no selection: callers retain their
// existing automatic path. Do not sort, normalize rates, or change dimensions.
inline std::optional<std::size_t> FindRefreshMode(const std::vector<Platform::DisplayMode>& modes,
                                                 Platform::Size size, Platform::RefreshRate refresh)
{
    if (!Platform::HasRefresh(refresh)) return std::nullopt;
    for (std::size_t i = 0; i < modes.size(); ++i)
        if (MatchesRefreshMode(modes[i], size, refresh)) return i;
    return std::nullopt;
}

// Engine policy over the owned catalog snapshot. Never fall through to another
// display. Return the selected depth as well as the rate so native mapping does
// not reintroduce a low-color variant rejected by the engine.
inline std::optional<Platform::DisplayMode> SelectPrimaryRefreshMode(
    const Platform::DisplayCatalog& catalog, Platform::Size size, Platform::RefreshRate refresh)
{
    if (!catalog.primaryDisplay || *catalog.primaryDisplay >= catalog.displays.size())
        return std::nullopt;
    const auto& modes = catalog.displays[*catalog.primaryDisplay].modes;
    const auto selected = FindRefreshMode(modes, size, refresh);
    return selected ? std::optional<Platform::DisplayMode>{modes[*selected]} : std::nullopt;
}

} // namespace GameDisplay
