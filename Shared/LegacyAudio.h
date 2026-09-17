#pragma once
#include <cstddef>
#include <cstdint>

namespace LegacyAudio {
inline std::size_t SampleCount(std::size_t length) { return length/2+length%2; }
inline bool DecodeLength(const std::uint8_t* p,std::size_t size,std::uint32_t& out)
{
    if(!p || size<4) return false;
    out=std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) |
        (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
    return true;
}
// length is serialized bytes, never a native sample count. Odd final bytes
// are low bytes with zero high bytes. Buffers must not overlap.
inline bool DecodePCM16(const std::uint8_t* p,std::size_t size,
                        std::int16_t* out,std::size_t capacity,std::size_t length)
{
    const auto samples=SampleCount(length);
    if(length>size || samples>capacity || (length && (!p || !out))) return false;
    for(std::size_t i=0;i<samples;++i) {
        const std::uint16_t v=std::uint16_t(p[2*i]) |
            (2*i+1<length ? std::uint16_t(p[2*i+1])<<8 : 0);
        out[i]=v<=INT16_MAX ? static_cast<std::int16_t>(v) :
            static_cast<std::int16_t>(-1-static_cast<std::int32_t>(UINT16_MAX-v));
    }
    return true;
}
}
