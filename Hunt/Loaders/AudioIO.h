#pragma once
#include "LoadValidate.h"
#include "../../Shared/LegacyAudio.h"
#include <array>
#include <algorithm>
#include <limits>

namespace EngineAudio {
inline bool ReadPCM16(HANDLE file,short* out,std::size_t capacity,std::size_t length)
{
    static_assert(sizeof(short)==2 && (std::numeric_limits<short>::min)()==INT16_MIN &&
                  (std::numeric_limits<short>::max)()==INT16_MAX,
                  "Existing audio backend requires signed 16-bit runtime samples");
    if(LegacyAudio::SampleCount(length)>capacity || (length && !out)) return false;
    std::array<std::uint8_t,4096> bytes;
    std::array<std::int16_t,2048> values;
    while(length) {
        const auto n=(std::min)(length,bytes.size());
        if(!ReadExact(file,bytes.data(),static_cast<DWORD>(n)) ||
           !LegacyAudio::DecodePCM16(bytes.data(),n,values.data(),values.size(),n)) return false;
        const auto samples=LegacyAudio::SampleCount(n);
        for(std::size_t i=0;i<samples;++i) out[i]=values[i];
        out+=samples;length-=n;
    }
    return true;
}
}
