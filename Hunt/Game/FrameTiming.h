#pragma once

#include <cstdint>

namespace FrameTiming {
// OptFpsLimit is validated by the existing config reader (indices 0..3).
constexpr std::int64_t TargetMicroseconds(int option)
{
    constexpr int rates[] = {0, 60, 120, 240};
    return rates[option] == 0 ? 0 : 1000000 / rates[option];
}

// Preserve integer truncation and counter-difference arithmetic. The caller
// supplies positive frequency and the same bounded intervals as the old limiter.
constexpr std::int64_t ElapsedMicroseconds(std::int64_t start,
                                         std::int64_t now,
                                         std::int64_t frequency)
{
    return (now - start) * 1000000 / frequency;
}
} // namespace FrameTiming
