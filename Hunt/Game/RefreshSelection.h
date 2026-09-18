#pragma once

#include "../Platform/Platform.h"

namespace GameDisplay {

constexpr bool HasRefresh(Platform::RefreshRate rate)
{
    return rate.numerator != 0 && rate.denominator != 0;
}

constexpr bool EqualRefresh(Platform::RefreshRate a, Platform::RefreshRate b)
{
    // uint32 * uint32 fits in uint64, including unreduced backend fractions.
    return HasRefresh(a) && HasRefresh(b) &&
        std::uint64_t{a.numerator} * b.denominator ==
        std::uint64_t{b.numerator} * a.denominator;
}

constexpr std::optional<std::uint32_t> IntegerRefreshHz(Platform::RefreshRate rate)
{
    if (!HasRefresh(rate) || rate.numerator % rate.denominator != 0)
        return std::nullopt;
    return rate.numerator / rate.denominator;
}

inline bool MatchesRefreshMode(const Platform::DisplayMode& mode, Platform::Size size,
                               Platform::RefreshRate refresh)
{
    return mode.size.width == size.width && mode.size.height == size.height &&
        mode.bitsPerPixel >= 16 && EqualRefresh(mode.refresh, refresh);
}

// Inactive/unsupported requests both return no selection: callers retain their
// existing automatic path. Do not sort, normalize rates, or change dimensions.
inline std::optional<std::size_t> FindRefreshMode(const std::vector<Platform::DisplayMode>& modes,
                                                 Platform::Size size, Platform::RefreshRate refresh)
{
    if (!HasRefresh(refresh)) return std::nullopt;
    for (std::size_t i = 0; i < modes.size(); ++i)
        if (MatchesRefreshMode(modes[i], size, refresh)) return i;
    return std::nullopt;
}

} // namespace GameDisplay
