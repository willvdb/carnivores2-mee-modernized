#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

// Legacy .CAR/.3DF values, never copied as object representations from disk.
// Decoders accept trailing bytes; short buffers leave the output untouched.
// Content/count policy belongs to the loader, independently of byte decoding.
namespace LegacyModel {
inline constexpr std::size_t VertexSize=16, FaceSize=64, ObjectSize=48;
inline constexpr std::size_t HeaderSize=16, CharacterHeaderSize=52;
inline constexpr std::size_t AnimationHeaderSize=40, ObjectAnimationHeaderSize=16;
inline constexpr std::size_t SampleSize=6, AssociationsSize=256;
struct Vertex { std::uint32_t x{}, y{}, z{}; std::int16_t owner{}, hide{}; };
struct Face {
    std::array<std::int32_t,3> indices{};
    std::array<std::int32_t,6> uv{}; // tax, tbx, tcx, tay, tby, tcy: integers!
    std::uint16_t flags{}, mask{};
    std::int32_t distant{}, next{}, group{};
    std::array<std::uint8_t,12> reserved{};
};
struct Object { std::array<std::uint8_t,32> name{}; Vertex origin{}; };
struct Header { std::int32_t vertices{}, faces{}, objects{}, textureBytes{}; };
struct CharacterHeader {
    std::array<std::uint8_t,32> name{};
    std::int32_t animations{}, sounds{}, vertices{}, faces{}, textureBytes{};
};
struct AnimationHeader {
    std::array<std::uint8_t,32> name{};
    std::int32_t kps{}, frames{};
};
struct ObjectAnimationHeader { std::int32_t type{}, vertices{}, kps{}, frames{}; };
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
              "Model coordinates require IEEE-754 binary32");
inline void SetFloat(float& f, std::uint32_t bits) { std::memcpy(&f,&bits,4); }
namespace detail {
inline std::uint16_t U16(const std::uint8_t* p)
{ return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8); }
inline std::uint32_t U32(const std::uint8_t* p)
{ return std::uint32_t(U16(p)) | (std::uint32_t(U16(p+2)) << 16); }
inline std::int16_t I16(const std::uint8_t* p)
{
    const auto v=U16(p);
    return v <= INT16_MAX ? static_cast<std::int16_t>(v)
                         : static_cast<std::int16_t>(-1 - (UINT16_MAX-v));
}
inline std::int32_t I32(const std::uint8_t* p)
{
    const auto v=U32(p);
    return v <= INT32_MAX ? static_cast<std::int32_t>(v)
                         : -1 - static_cast<std::int32_t>(UINT32_MAX-v);
}
}
inline bool DecodeInt32(const std::uint8_t* p, std::size_t n, std::int32_t& v)
{
    if (!p || n<4) return false;
    v=detail::I32(p); return true;
}
inline bool DecodeVertex(const std::uint8_t* p, std::size_t n, Vertex& v)
{
    if (!p || n<VertexSize) return false;
    v={detail::U32(p),detail::U32(p+4),detail::U32(p+8),detail::I16(p+12),detail::I16(p+14)};
    return true;
}
inline bool DecodeFace(const std::uint8_t* p, std::size_t n, Face& v)
{
    if (!p || n<FaceSize) return false;
    for(unsigned i=0;i<3;++i) v.indices[i]=detail::I32(p+4*i);
    for(unsigned i=0;i<6;++i) v.uv[i]=detail::I32(p+12+4*i);
    v.flags=detail::U16(p+36); v.mask=detail::U16(p+38);
    v.distant=detail::I32(p+40); v.next=detail::I32(p+44); v.group=detail::I32(p+48);
    std::memcpy(v.reserved.data(),p+52,12); return true;
}
inline bool DecodeObject(const std::uint8_t* p, std::size_t n, Object& v)
{
    if (!p || n<ObjectSize) return false;
    std::memcpy(v.name.data(),p,32); DecodeVertex(p+32,n-32,v.origin); return true;
}
inline bool DecodeHeader(const std::uint8_t* p, std::size_t n, Header& v)
{
    if (!p || n<HeaderSize) return false;
    v={detail::I32(p),detail::I32(p+4),detail::I32(p+8),detail::I32(p+12)}; return true;
}
inline bool DecodeCharacterHeader(const std::uint8_t* p, std::size_t n, CharacterHeader& v)
{
    if (!p || n<CharacterHeaderSize) return false;
    std::memcpy(v.name.data(),p,32);
    v.animations=detail::I32(p+32); v.sounds=detail::I32(p+36);
    v.vertices=detail::I32(p+40); v.faces=detail::I32(p+44); v.textureBytes=detail::I32(p+48);
    return true;
}
inline bool DecodeAnimationHeader(const std::uint8_t* p, std::size_t n, AnimationHeader& v)
{
    if (!p || n<AnimationHeaderSize) return false;
    std::memcpy(v.name.data(),p,32); v.kps=detail::I32(p+32); v.frames=detail::I32(p+36);
    return true;
}
inline bool DecodeObjectAnimationHeader(const std::uint8_t* p, std::size_t n, ObjectAnimationHeader& v)
{
    if (!p || n<ObjectAnimationHeaderSize) return false;
    v={detail::I32(p),detail::I32(p+4),detail::I32(p+8),detail::I32(p+12)}; return true;
}
// count is the number of scalar samples, three per vertex/frame. Division
// avoids overflow even for SIZE_MAX. Destination has at least count elements.
inline bool DecodeSamples(const std::uint8_t* p, std::size_t n, std::int16_t* v, std::size_t count)
{
    if (count>n/2 || (count && (!p || !v))) return false;
    for(std::size_t i=0;i<count;++i) v[i]=detail::I16(p+2*i);
    return true;
}
// Odd byte counts are accepted by the old HARD3D loader: the last low byte
// occupies a zero-initialized word. Destination has ceil(n/2) words.
inline bool DecodeTexture(const std::uint8_t* p, std::size_t n, std::uint16_t* v, std::size_t capacity)
{
    if (n/2+n%2>capacity || (n && (!p || !v))) return false;
    for(std::size_t i=0;i<n/2;++i) v[i]=detail::U16(p+2*i);
    if (n%2) v[n/2]=p[n-1];
    return true;
}
inline bool DecodeAssociations(const std::uint8_t* p, std::size_t n, std::array<std::int32_t,64>& v)
{
    if (!p || n<AssociationsSize) return false;
    for(unsigned i=0;i<64;++i) v[i]=detail::I32(p+4*i);
    return true;
}
} // namespace LegacyModel
