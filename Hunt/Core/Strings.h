#pragma once
#include <cstddef>
#include <cstring>
namespace LegacyText {
inline int Compare(const char* a, const char* b, std::size_t limit = static_cast<std::size_t>(-1))
{
    for (std::size_t i=0; i<limit; ++i) {
        unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if (x >= 'A' && x <= 'Z') x += 'a'-'A';
        if (y >= 'A' && y <= 'Z') y += 'a'-'A';
        if (x != y || !x) return static_cast<int>(x)-y;
    }
    return 0;
}
inline bool Copy(char* dst, std::size_t size, const char* src)
{
    if (!dst || !size || !src) return false;
    const auto length = std::strlen(src);
    if (length >= size) { dst[0] = 0; return false; }
    std::memcpy(dst, src, length+1);
    return true;
}
}
