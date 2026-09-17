#pragma once
#include "ResourceSerialization.h"
#include "LoadValidate.h"

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
}
