#pragma once

#include "../Platform/Platform.h"
#include <array>

// Engine policy, independent of how a backend discovers its display modes.
namespace GameDisplay {
struct Resolutions {
    std::array<Platform::Size, 128> modes{};
    int count = 0;

    void Add(Platform::Size size)
    {
        for (int i = 0; i < count; ++i)
            if (modes[i].width == size.width && modes[i].height == size.height)
                return;
        if (count == static_cast<int>(modes.size())) return;
        modes[count++] = size;
    }

    void EnsureFallback()
    {
        if (count == 0) Add({800, 600});
    }
};

inline Resolutions SelectResolutions(const Platform::DisplayInfo& display)
{
    Resolutions result;
    for (const auto& mode : display.modes) {
        if (mode.bitsPerPixel < 16) continue;
        if (mode.size.width > display.desktop.width ||
            mode.size.height > display.desktop.height) continue;
        result.Add(mode.size);
    }
    // Preserve append order and the historical cap, including when already full.
    result.Add(display.desktop);
    result.EnsureFallback();
    return result;
}
} // namespace GameDisplay
