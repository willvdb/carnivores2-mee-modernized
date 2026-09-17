#pragma once
#include "../../Shared/LegacyMap.h"
#include "LoadValidate.h"
#include <algorithm>
#include <array>
#include <limits>

// Only this engine boundary knows Win32 handles and runtime std::uint16_t storage.
namespace EngineMap {
inline bool ReadBytes(Platform::FileHandle file, void* out, std::size_t capacity, std::size_t count)
{
    if (count > capacity || count > UINT32_MAX || (count && !out)) return false;
    return !count || ReadExact(file, out, static_cast<std::uint32_t>(count));
}

inline bool ReadWords(Platform::FileHandle file, std::uint16_t* out, std::size_t capacity, std::size_t count)
{
    static_assert((std::numeric_limits<std::uint16_t>::max)() >= UINT16_MAX,
                  "Runtime map words must hold all 16 persistent bits");
    if (count > capacity || count > (std::numeric_limits<std::size_t>::max)() / 2 ||
        (count && !out)) return false;
    std::array<std::uint8_t,4096> bytes;
    std::array<std::uint16_t,2048> values;
    while (count) {
        const auto n = (std::min)(count, values.size());
        if (!ReadExact(file, bytes.data(), static_cast<std::uint32_t>(2*n)) ||
            !LegacyMap::DecodeWords(bytes.data(), 2*n, values.data(), values.size(), n))
            return false;
        for (std::size_t i = 0; i < n; ++i) out[i] = values[i];
        out += n;
        count -= n;
    }
    return true;
}

template<std::size_t Rows, std::size_t Cols>
bool ReadBytePlane(Platform::FileHandle file, unsigned char (&out)[Rows][Cols])
{
    static_assert((Rows == LegacyMap::Width && Cols == LegacyMap::Width) ||
                  (Rows == LegacyMap::RegionWidth && Cols == LegacyMap::RegionWidth),
                  "Legacy map byte-plane dimensions are fixed");
    static_assert(std::numeric_limits<unsigned char>::digits == 8,
                  "Legacy map bytes require eight-bit storage");
    return ReadBytes(file, out, sizeof(out), Rows * Cols);
}

template<std::size_t Rows, std::size_t Cols>
bool ReadWordPlane(Platform::FileHandle file, std::uint16_t (&out)[Rows][Cols])
{
    static_assert(Rows == LegacyMap::Width && Cols == LegacyMap::Width,
                  "Legacy map word-plane dimensions are fixed");
    // Stay within each C++ row array, rather than walking a flattened pointer.
    for (auto& row : out)
        if (!ReadWords(file, row, Cols, Cols)) return false;
    return true;
}

inline bool SkipLightBytes(Platform::FileHandle file, std::size_t count)
{
    // SetFilePointer alone permits seeking past EOF. Bound these fixed skips
    // against the actual file so missing unselected planes also fail promptly.
    if (count > 2 * LegacyMap::BytePlaneSize) return false;
    const auto position = Platform::SeekFile(file, 0, Platform::SeekOrigin::Current);
    const auto size = Platform::FileSize(file);
    if (position < 0 || size < position || size - position < static_cast<std::int64_t>(count)) return false;
    return Platform::SeekFile(file, count, Platform::SeekOrigin::Current) >= 0;
}

template<std::size_t Rows, std::size_t Cols>
bool ReadLightPlane(Platform::FileHandle file, unsigned char (&out)[Rows][Cols], int day)
{
    static_assert(Rows == LegacyMap::Width && Cols == LegacyMap::Width,
                  "Legacy light maps are full-sized byte planes");
    if (day < 0 || day >= static_cast<int>(LegacyMap::LightCount)) return false;
    return SkipLightBytes(file, LegacyMap::BytePlaneSize * day) &&
           ReadBytePlane(file, out) &&
           SkipLightBytes(file, LegacyMap::BytePlaneSize * (2-day));
}
}
