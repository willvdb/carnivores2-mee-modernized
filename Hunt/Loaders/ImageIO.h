#pragma once
#include "LoadValidate.h"
#include "../../Shared/LegacyImage.h"
#include <array>
#include <algorithm>
#include <limits>

namespace EngineImage {
inline bool ReadPixels(Platform::FileHandle file,std::uint16_t* out,std::size_t capacity,std::size_t count)
{
    static_assert((std::numeric_limits<std::uint16_t>::max)()>=UINT16_MAX,"Runtime pixel range");
    if(count>capacity || count>SIZE_MAX/2 || (count && !out)) return false;
    std::array<std::uint8_t,4096> bytes;
    std::array<std::uint16_t,2048> values;
    while(count) {
        const auto n=(std::min)(count,values.size());
        if(!ReadExact(file,bytes.data(),static_cast<std::uint32_t>(n*2)) ||
           !LegacyImage::DecodePixels(bytes.data(),n*2,values.data(),values.size(),n)) return false;
        for(std::size_t i=0;i<n;++i) out[i]=values[i];
        out+=n;count-=n;
    }
    return true;
}
}
