#pragma once
#include "ResourceSerialization.h"
#include "LoadValidate.h"
#include <algorithm>

// Win32 I/O stays at the engine boundary. No handle enters LegacyResource.
namespace EngineResource {
inline bool Read(HANDLE file, int& out)
{
    std::uint8_t bytes[4]; std::int32_t value;
    if (!ReadExact(file,bytes,4) || !LegacyResource::DecodeInt32(bytes,4,value)) return false;
    out=value; return true;
}
namespace detail {
template<std::size_t N, class Value, class Runtime, class Decoder>
bool ReadRecord(HANDLE file, Runtime& out, Decoder decode)
{
    std::array<std::uint8_t,N> bytes; Value value;
    if (!ReadExact(file,bytes.data(),static_cast<DWORD>(N)) || !decode(bytes.data(),N,value)) return false;
    ToRuntime(value,out); return true;
}
}
inline bool Read(HANDLE file, int (&out)[3][3])
{ return detail::ReadRecord<LegacyResource::ColorTableSize,LegacyResource::ColorTable>(file,out,LegacyResource::DecodeColors); }
inline bool Read(HANDLE file, TObjInfo& out)
{ return detail::ReadRecord<LegacyResource::ObjectInfoSize,LegacyResource::ObjectInfo>(file,out,LegacyResource::DecodeObjectInfo); }
inline bool Read(HANDLE file, TFogEntity& out)
{ return detail::ReadRecord<LegacyResource::FogSize,LegacyResource::Fog>(file,out,LegacyResource::DecodeFog); }
inline bool Read(HANDLE file, TWaterEntity& out)
{ return detail::ReadRecord<LegacyResource::WaterSize,LegacyResource::Water>(file,out,LegacyResource::DecodeWater); }
inline bool Read(HANDLE file, TRD (&out)[16])
{
    std::array<std::uint8_t,LegacyResource::RandomEffectsSize> bytes; LegacyResource::RandomEffects values;
    if (!ReadExact(file,bytes.data(),static_cast<DWORD>(bytes.size())) ||
        !LegacyResource::DecodeRandomEffects(bytes.data(),bytes.size(),values)) return false;
    for(unsigned i=0;i<16;++i) ToRuntime(values[i],out[i]);
    return true;
}
// Bounded chunks avoid a second image-sized allocation. Each chunk contains
// complete words; runtime assignment is numeric, independent of host endian.
inline bool ReadTexture(HANDLE file, unsigned short* out, std::size_t capacity, std::size_t words)
{
    static_assert((std::numeric_limits<unsigned short>::max)()>=UINT16_MAX,
                  "Runtime pixels must hold legacy uint16 words");
    if (words>capacity || (words && !out)) return false;
    std::array<std::uint8_t,4096> bytes;
    std::array<std::uint16_t,2048> values;
    while (words) {
        const std::size_t count=(std::min)(words,values.size());
        if (!ReadExact(file,bytes.data(),static_cast<DWORD>(count*2)) ||
            !LegacyResource::DecodeTexture(bytes.data(),count*2,values.data(),values.size(),count)) return false;
        for(std::size_t i=0;i<count;++i) out[i]=values[i];
        out+=count; words-=count;
    }
    return true;
}
}
