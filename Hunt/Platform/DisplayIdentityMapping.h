#pragma once
#include "Platform.h"

namespace Platform::DisplayIdentityDetails {
struct NativeIdentity { DisplayBounds bounds; std::optional<DisplayIdentity> identity; };
// Associate facts only on a bijective full-rectangle match. Neither enumeration
// order nor a missing identity licenses picking another matching monitor.
inline void Associate(DisplayCatalog& catalog, const std::vector<NativeIdentity>& native)
{
    for (auto& display : catalog.displays) {
        display.identity.reset();
        if (!display.bounds) continue;
        std::size_t sourceMatches = 0, nativeMatches = 0;
        for (const auto& source : catalog.displays)
            if (source.bounds && EqualDisplayBounds(*source.bounds, *display.bounds)) ++sourceMatches;
        const NativeIdentity* match = nullptr;
        for (const auto& candidate : native)
            if (EqualDisplayBounds(candidate.bounds, *display.bounds)) { ++nativeMatches; match = &candidate; }
        if (sourceMatches == 1 && nativeMatches == 1) display.identity = match->identity;
    }
}
} // namespace Platform::DisplayIdentityDetails
