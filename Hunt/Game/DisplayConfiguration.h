#pragma once
#include "MonitorPreference.h"
#include "ResolutionSelection.h"

namespace GameDisplay {
// Engine-owned presentation state. Zero dimensions preserve the pre-profile
// unspecified startup size; this is NOT a new persisted record or profile ABI.
// Linux keeps requested size here; WinW/H hold effective drawable pixels.
// Windows retains its existing applied-client-size compatibility behavior.
struct Configuration {
    Platform::Size size{};
    Platform::WindowMode mode = Platform::WindowMode::Exclusive;
    MonitorPreference monitor;
    Platform::RefreshRate refresh{};
};
struct LegacyPresentation {
    Platform::Size size;
    std::int32_t fullscreen;
    std::int32_t borderless;
};
inline LegacyPresentation ProjectLegacyPresentation(const Configuration& config)
{
    return {config.size, config.mode == Platform::WindowMode::Exclusive,
            config.mode == Platform::WindowMode::Borderless};
}
inline void ApplyLegacyProfileResolution(Configuration& config, const Platform::Size* modes,
                                         int count, std::int32_t& ordinal)
{
    const auto selected = ResolveLegacyResolution(modes, count, ordinal);
    config.size = selected.size;
    ordinal = selected.ordinal;
}
// The parser validates dimensions first. Keep the historical unmatched config
// (-1, CurRes untouched) versus CLI (both previous ordinals retained) distinction.
inline void ApplyConfigResolution(Configuration& config, Platform::Size size,
                                  const Platform::Size* modes, int count, std::int32_t& ordinal)
{
    config.size = size;
    ordinal = FindResolution(modes, count, size);
}
inline void ApplyCommandLineResolution(Configuration& config, Platform::Size size,
                                       const Platform::Size* modes, int count,
                                       std::int32_t& ordinal, int& current)
{
    config.size = size;
    const int index = FindResolution(modes, count, size);
    if (index >= 0) { ordinal = index; current = index; }
}
inline bool ApplyConfigWindowMode(Configuration& config, int value, bool software = false)
{
    if (value < 0 || value > 2) return false;
    if (software && value == 2) value = 1;
    config.mode = value == 1 ? Platform::WindowMode::Exclusive :
                  value == 2 ? Platform::WindowMode::Borderless : Platform::WindowMode::Windowed;
    return true;
}
inline void ToggleWindowMode(Configuration& config)
{
    // Borderless -> exclusive; exclusive -> windowed; windowed -> exclusive.
    config.mode = config.mode == Platform::WindowMode::Exclusive ?
                  Platform::WindowMode::Windowed : Platform::WindowMode::Exclusive;
}
inline void ApplyClientSize(Configuration& config, Platform::Size size)
{
    if (size.width > 0 && size.height > 0) config.size = size;
}
} // namespace GameDisplay
