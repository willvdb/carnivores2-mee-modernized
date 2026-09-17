#pragma once
#include <cstddef>
#include <cstdint>

// Only the byte contracts consumed by the legacy image loaders. No I/O or
// format-policy checks here. Buffers must not overlap; failures leave output.
namespace LegacyImage {
inline std::uint16_t U16(const std::uint8_t* p)
{ return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8); }
inline std::uint32_t U32(const std::uint8_t* p)
{ return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) | (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24); }
inline std::int32_t I32(const std::uint8_t* p)
{
    const auto v=U32(p);
    return v<=INT32_MAX ? static_cast<std::int32_t>(v) : -1-static_cast<std::int32_t>(UINT32_MAX-v);
}
constexpr std::size_t TgaHeaderSize=18, BmpHeaderSize=54;
struct TgaHeader {
    std::uint8_t idLength, colorMapType, imageType;
    std::uint16_t colorMapOffset, colorMapLength;
    std::uint8_t colorMapBits;
    std::uint16_t xOrigin, yOrigin, width, height;
    std::uint8_t bits, descriptor;
};
inline bool DecodeTgaHeader(const std::uint8_t* p,std::size_t size,TgaHeader& out)
{
    if(!p || size<TgaHeaderSize) return false;
    out={p[0],p[1],p[2],U16(p+3),U16(p+5),p[7],U16(p+8),U16(p+10),
         U16(p+12),U16(p+14),p[16],p[17]};
    return true;
}
struct BmpHeader {
    std::uint16_t signature;
    std::uint32_t fileSize;
    std::uint16_t reserved1, reserved2;
    std::uint32_t pixelOffset, headerSize;
    std::int32_t width, height;
    std::uint16_t planes, bits;
    std::uint32_t compression, imageSize;
    std::int32_t xPixelsPerMeter, yPixelsPerMeter;
    std::uint32_t colorsUsed, colorsImportant;
};
inline bool DecodeBmpHeader(const std::uint8_t* p,std::size_t size,BmpHeader& out)
{
    if(!p || size<BmpHeaderSize) return false;
    out={U16(p),U32(p+2),U16(p+6),U16(p+8),U32(p+10),U32(p+14),
         I32(p+18),I32(p+22),U16(p+26),U16(p+28),U32(p+30),U32(p+34),
         I32(p+38),I32(p+42),U32(p+46),U32(p+50)};
    return true;
}
inline bool DecodePixels(const std::uint8_t* p,std::size_t size,
                         std::uint16_t* out,std::size_t capacity,std::size_t count)
{
    if(count>size/2 || count>capacity || (count && (!p || !out))) return false;
    for(std::size_t i=0;i<count;++i) out[i]=U16(p+2*i);
    return true;
}
}
