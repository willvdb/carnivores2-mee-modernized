#pragma once

#include "../Platform/Platform.h"

// Engine selection policy over the current ordered list. Dimensions identify
// a resolution; ordinals are only adapters for the legacy profile option.
namespace GameDisplay {

// modes contains count entries when count > 0; otherwise it is not accessed.
// Return the first exact dimension match, or -1. No sorting or size validation.
inline int FindResolution(const Platform::Size* modes, int count, Platform::Size size)
{
    for (int i = 0; i < count; ++i)
        if (modes[i].width == size.width && modes[i].height == size.height)
            return i;
    return -1;
}

struct LegacyResolution {
    Platform::Size size;
    std::int32_t ordinal;
};

inline LegacyResolution ResolveLegacyResolution(const Platform::Size* modes, int count,
                                                std::int32_t ordinal)
{
    // SetupRes historically leaves OptRes untouched when no list is available.
    if (count <= 0) return {{800, 600}, ordinal};
    if (ordinal < 0 || ordinal >= count) {
        ordinal = FindResolution(modes, count, {800, 600});
        if (ordinal < 0) ordinal = 0;
    }
    return {modes[ordinal], ordinal};
}

} // namespace GameDisplay
