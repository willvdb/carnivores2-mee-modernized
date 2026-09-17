#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Independent literal disk contract: do not build fixtures with the codec.
namespace MapGolden {
inline constexpr std::size_t Full = 1048576, Small = 262144, Size = 14155776;
inline constexpr std::array<std::size_t,12> Offsets = {
    0,1048576,3145728,5242880,6291456,8388608,9437184,10485760,
    11534336,12582912,13631488,13893632
};
inline constexpr std::array<std::uint8_t,14> Words = {
    0,0, 1,0, 255,0, 0,1, 0x34,0x12, 0x34,0x92, 255,255
};
inline constexpr std::array<std::uint16_t,7> Values = {0,1,255,256,0x1234,0x9234,0xffff};
inline constexpr std::array<std::uint16_t,14> Flags = {
    0,0xffff,1,2,4,8,0x10,0x20,0x80,0x8000,0x100,0x200,0x1234,0x9234
};
}
