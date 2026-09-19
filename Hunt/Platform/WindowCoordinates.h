#pragma once
#include "Platform.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace Platform::Coordinates {
inline float Scale(float value, std::int32_t from, std::int32_t to)
{
    if (from <= 0 || to <= 0 || !std::isfinite(value)) return 0;
    return static_cast<float>(static_cast<double>(value) * to / from);
}
inline std::int32_t Integer(float value)
{
    const double bounded = (std::clamp)(static_cast<double>(value),
        static_cast<double>((std::numeric_limits<std::int32_t>::min)()),
        static_cast<double>((std::numeric_limits<std::int32_t>::max)()));
    return static_cast<std::int32_t>(bounded);
}
inline MouseDelta Convert(MouseDelta point, Size from, Size to)
{
    return {Scale(point.x, from.width, to.width), Scale(point.y, from.height, to.height)};
}
} // namespace Platform::Coordinates
