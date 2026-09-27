#pragma once
#include <cerrno>
#include <cstdlib>

// Strict scalar parsing for config.cfg values (engine loader: LoadConfig in
// Game/EngineInit.cpp).  A bare strtol/strtof accepts "abc" (0) and "1abc"
// (1) without reporting a problem, which would silently select a valid but
// unintended setting; every value must consume the whole token and then pass
// an inclusive range check.  Pure header so the rules stay unit-testable.
inline bool ParseConfigInt(const char* text, int minValue, int maxValue, int& out)
{
    if (text == nullptr || *text == '\0') return false;
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE) return false;
    // Compare in long: an out-of-range input is rejected instead of wrapping
    // through the int cast.
    if (value < static_cast<long>(minValue) || value > static_cast<long>(maxValue)) return false;
    out = static_cast<int>(value);
    return true;
}

// Closed range is written inverted on purpose: NaN fails every ordered
// comparison, so "nan" is rejected rather than slipping past the bounds.
inline bool ParseConfigFloat(const char* text, float minValue, float maxValue, float& out)
{
    if (text == nullptr || *text == '\0') return false;
    char* end = nullptr;
    errno = 0;
    const float value = std::strtof(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE) return false;
    if (!(value >= minValue && value <= maxValue)) return false;
    out = value;
    return true;
}
