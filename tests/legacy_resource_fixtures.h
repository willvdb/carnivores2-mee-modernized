#pragma once
#include <array>
#include <cstdint>

// Independent synthetic byte builders. No production codec is used here.
namespace ResourceGolden {
template<class Buffer> void Put32(Buffer& b, unsigned offset, std::uint32_t value)
{
    for (unsigned i=0;i<4;++i) { b[offset+i]=value%256; value/=256; }
}
inline constexpr std::array<std::uint32_t,11> ObjectInts{
    0xffffffff,0x80000000,0x7fffffff,0xffffff80,0x12345678,0,17,
    0x89abcdef,0xfffffffe,0x10203040,0x87654321};
inline auto Object()
{
    std::array<std::uint8_t,64> b{};
    for(unsigned i=0;i<11;++i) Put32(b,i*4,ObjectInts[i]);
    Put32(b,44,0x80000000);
    for(unsigned i=48;i<64;++i) b[i]=static_cast<std::uint8_t>(i*7);
    return b;
}
inline auto Colors()
{
    std::array<std::uint8_t,72> b{};
    for(unsigned i=0;i<18;++i) Put32(b,4*i,0x81234560+i);
    return b;
}
inline constexpr std::array<std::uint8_t,20> Fog{
    0x98,0xba,0xdc,0xfe, 0,0,0,0x80, 0xff,0xff,0xff,0xff,
    1,0,0,0, 0x45,0x23,0xc1,0x7f};
inline auto Effects()
{
    std::array<std::uint8_t,256> b{};
    for(unsigned i=0;i<16;++i) {
        Put32(b,i*16,0x80000000+i); Put32(b,i*16+4,0xffffffff-i);
        Put32(b,i*16+8,0x12345678+i);
        b[i*16+12]=i; b[i*16+13]=0x92;
        b[i*16+14]=0xff-i; b[i*16+15]=0xab;
    }
    return b;
}
inline constexpr std::array<std::uint8_t,16> Water{
    0xfe,0xff,0xff,0xff, 0x78,0x56,0x34,0x12,
    0,0,0x40,0x3f, 0x98,0xba,0xdc,0xfe};
inline constexpr std::array<std::uint8_t,10> Pixels{0,0,0xff,0xff,0x34,0x92,0x12,0x34,0xab,0xcd};
inline constexpr std::array<std::uint8_t,13> PCM{0,0x80,0xff,0x7f,0xff,0xff,0,0,0x34,0x12,0xcc,0xed,0xab};
}
