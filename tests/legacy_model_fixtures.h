#pragma once
#include <array>
#include <cstdint>

// Independent synthetic bytes: no production codec helpers or game content.
namespace ModelGolden {
template<std::size_t N>
void Put32(std::array<std::uint8_t, N>& b, std::size_t at, std::uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) { b[at+i] = v % 256; v /= 256; }
}
inline auto Vertex()
{
    return std::array<std::uint8_t,16>{0,0,0x80,0x3f, 0,0,0x20,0xc0,
                                     0,0,0,0x80, 0,0x80, 0xff,0x7f};
}
inline auto Face()
{
    std::array<std::uint8_t,64> b{};
    Put32(b,0,0x12345678); Put32(b,4,2); Put32(b,8,0xffffffff);
    const std::uint32_t uv[]{0xffffffff,0,255,128,17,0x12345678};
    for(unsigned i=0;i<6;++i) Put32(b,12+4*i,uv[i]);
    b[36]=0x35; b[37]=0xa1; b[38]=0x87; b[39]=0x69;
    Put32(b,40,0x87654321); Put32(b,44,0xfffffffe); Put32(b,48,0x10203040);
    for(unsigned i=0;i<12;++i) b[52+i]=0x91+i*7;
    return b;
}
inline auto Object()
{
    std::array<std::uint8_t,48> b{};
    for(unsigned i=0;i<32;++i) b[i]=0x41+i;
    b[2]=0; // remaining name bytes must survive
    Put32(b,32,0x3f000000); Put32(b,36,0xc1200000); Put32(b,40,0x80000000);
    b[44]=0xfe; b[45]=0xff; b[46]=0x34; b[47]=0x12;
    return b;
}
inline constexpr std::array<std::uint8_t,12> Samples{
    0,0x80, 0xff,0x7f, 0xff,0xff, 0,0, 0x34,0x12, 0xcc,0xed};
}
