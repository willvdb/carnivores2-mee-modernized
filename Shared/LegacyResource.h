#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// .RSC values only. These objects are never copied to/from a file as structs.
// Float fields transport IEEE-754 binary32 bits until runtime adaptation.
namespace LegacyResource {
inline constexpr std::size_t ObjectInfoSize=64, FogSize=20, RandomEffectSize=16, WaterSize=16;
inline constexpr std::size_t ColorTableSize=36, RandomEffectsSize=256;
struct ObjectInfo {
    std::int32_t radius{}, yLo{}, yHi{}, lineLength{}, lineIntensity{}, circleRadius{}, circleIntensity{};
    std::int32_t flags{}, groundRadius{}, defaultLight{}, lastAnimationTime{};
    std::uint32_t boundRadius{};
    std::array<std::uint8_t,16> reserved{};
};
struct Fog { std::int32_t rgb{}; std::uint32_t yBegin{}; std::int32_t mortal{}; std::uint32_t transparency{}, limit{}; };
struct RandomEffect { std::int32_t number{}, volume{}, frequency{}; std::uint16_t environment{}, flags{}; };
struct Water { std::int32_t texture{}, level{}; std::uint32_t transparency{}; std::int32_t rgb{}; };
using ColorTable=std::array<std::int32_t,9>;
using RandomEffects=std::array<RandomEffect,16>;
namespace detail {
inline std::uint16_t U16(const std::uint8_t* p)
{ return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8); }
inline std::uint32_t U32(const std::uint8_t* p)
{ return std::uint32_t(U16(p)) | (std::uint32_t(U16(p+2)) << 16); }
inline std::int32_t I32(const std::uint8_t* p)
{
    const auto v=U32(p);
    return v<=INT32_MAX ? static_cast<std::int32_t>(v) : -1-static_cast<std::int32_t>(UINT32_MAX-v);
}
inline std::int16_t I16(const std::uint8_t* p)
{
    const auto v=U16(p);
    return v<=INT16_MAX ? static_cast<std::int16_t>(v) : static_cast<std::int16_t>(-1-(UINT16_MAX-v));
}
}
// Bounds are checked before any output mutation. Trailing bytes are accepted.
inline bool DecodeInt32(const std::uint8_t* p, std::size_t n, std::int32_t& v)
{
    if (!p || n<4) return false;
    v=detail::I32(p); return true;
}
inline bool DecodeColors(const std::uint8_t* p, std::size_t n, ColorTable& v)
{
    if (!p || n<ColorTableSize) return false;
    for(unsigned i=0;i<9;++i) v[i]=detail::I32(p+4*i);
    return true;
}
inline bool DecodeObjectInfo(const std::uint8_t* p, std::size_t n, ObjectInfo& v)
{
    if (!p || n<ObjectInfoSize) return false;
    v.radius=detail::I32(p); v.yLo=detail::I32(p+4); v.yHi=detail::I32(p+8);
    v.lineLength=detail::I32(p+12); v.lineIntensity=detail::I32(p+16);
    v.circleRadius=detail::I32(p+20); v.circleIntensity=detail::I32(p+24);
    v.flags=detail::I32(p+28); v.groundRadius=detail::I32(p+32);
    v.defaultLight=detail::I32(p+36); v.lastAnimationTime=detail::I32(p+40);
    v.boundRadius=detail::U32(p+44);
    for(unsigned i=0;i<16;++i) v.reserved[i]=p[48+i];
    return true;
}
inline bool DecodeFog(const std::uint8_t* p, std::size_t n, Fog& v)
{
    if (!p || n<FogSize) return false;
    v={detail::I32(p),detail::U32(p+4),detail::I32(p+8),detail::U32(p+12),detail::U32(p+16)};
    return true;
}
inline bool DecodeRandomEffect(const std::uint8_t* p, std::size_t n, RandomEffect& v)
{
    if (!p || n<RandomEffectSize) return false;
    v={detail::I32(p),detail::I32(p+4),detail::I32(p+8),detail::U16(p+12),detail::U16(p+14)};
    return true;
}
inline bool DecodeRandomEffects(const std::uint8_t* p, std::size_t n, RandomEffects& v)
{
    if (!p || n<RandomEffectsSize) return false;
    for(unsigned i=0;i<16;++i) DecodeRandomEffect(p+16*i,16,v[i]);
    return true;
}
inline bool DecodeWater(const std::uint8_t* p, std::size_t n, Water& v)
{
    if (!p || n<WaterSize) return false;
    v={detail::I32(p),detail::I32(p+4),detail::U32(p+8),detail::I32(p+12)};
    return true;
}
// Resource images have an even, fixed byte count. count is in words, not bytes.
inline bool DecodeTexture(const std::uint8_t* p, std::size_t n,
                          std::uint16_t* out, std::size_t capacity, std::size_t count)
{
    if (count>n/2 || count>capacity || (count && (!p || !out))) return false;
    for(std::size_t i=0;i<count;++i) out[i]=detail::U16(p+2*i);
    return true;
}
// length is the serialized byte length; consume no padding for an odd final byte.
inline bool DecodePCM16(const std::uint8_t* p, std::size_t n,
                        std::int16_t* out, std::size_t capacity, std::size_t length)
{
    if (length>n || length/2+length%2>capacity || (length && (!p || !out))) return false;
    for(std::size_t i=0;i<length/2;++i) out[i]=detail::I16(p+2*i);
    if (length%2) out[length/2]=p[length-1];
    return true;
}
} // namespace LegacyResource
