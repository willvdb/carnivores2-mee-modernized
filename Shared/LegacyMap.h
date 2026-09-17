#pragma once
#include <cstddef>
#include <cstdint>

// Carnivores 2 / MEE .MAP bytes, independent of engine storage and OS I/O.
namespace LegacyMap {
inline constexpr std::size_t Width = 1024;
inline constexpr std::size_t RegionWidth = 512;
inline constexpr std::size_t BytePlaneSize = Width * Width;
inline constexpr std::size_t WordPlaneSize = 2 * BytePlaneSize;
inline constexpr std::size_t RegionPlaneSize = RegionWidth * RegionWidth;
inline constexpr std::size_t HeightOffset = 0;
inline constexpr std::size_t Texture1Offset = HeightOffset + BytePlaneSize;
inline constexpr std::size_t Texture2Offset = Texture1Offset + WordPlaneSize;
inline constexpr std::size_t ObjectOffset = Texture2Offset + WordPlaneSize;
inline constexpr std::size_t FlagsOffset = ObjectOffset + BytePlaneSize;
inline constexpr std::size_t LightOffset = FlagsOffset + WordPlaneSize;
inline constexpr std::size_t LightCount = 3;
inline constexpr std::size_t WaterOffset = LightOffset + LightCount * BytePlaneSize;
inline constexpr std::size_t ObjectHeightOffset = WaterOffset + BytePlaneSize;
inline constexpr std::size_t FogOffset = ObjectHeightOffset + BytePlaneSize;
inline constexpr std::size_t AmbientOffset = FogOffset + RegionPlaneSize;
inline constexpr std::size_t FileSize = AmbientOffset + RegionPlaneSize;

// Decode complete uint16 values, without interpreting indices or clearing flags.
// Bounds/null checks precede all writes, using division to avoid overflow.
// Empty operations succeed; nonempty input and output must not overlap.
inline bool DecodeWords(const std::uint8_t* bytes, std::size_t size,
                        std::uint16_t* out, std::size_t capacity, std::size_t count)
{
    if (count > size / 2 || count > capacity || (count && (!bytes || !out)))
        return false;
    for (std::size_t i = 0; i < count; ++i)
        out[i] = static_cast<std::uint16_t>(std::uint16_t(bytes[2*i]) |
                                          (std::uint16_t(bytes[2*i+1]) << 8));
    return true;
}
}
