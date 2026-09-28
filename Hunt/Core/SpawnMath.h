#pragma once

#include <cmath>

inline bool IsValidSelectionRatio(float ratio)
{
    return std::isfinite(ratio) && ratio >= 0.0f;
}

// Ratios of zero exclude an entry when any positive weight exists. Legacy
// all-zero packs fall back to the first member, which was the old spawn code's
// initialized follower type. The final positive entry is rounding-safe.
inline int SelectWeightedRatioIndex(const float* ratios, int count,
                                    float selector)
{
    if (!ratios || count <= 0 || !std::isfinite(selector)
        || selector < 0.0f)
        return -1;

    for (int i = 0; i < count; ++i) {
        if (!IsValidSelectionRatio(ratios[i]))
            return -1;
    }

    int lastPositive = -1;
    for (int i = 0; i < count; ++i) {
        const float ratio = ratios[i];
        if (ratio == 0.0f)
            continue;

        lastPositive = i;
        if (selector <= ratio)
            return i;
        selector -= ratio;
    }

    return lastPositive >= 0 ? lastPositive : 0;
}
