#pragma once
#include <cstdint>
#include <vector>
namespace MediaGolden {
inline void Put(std::vector<std::uint8_t>& b, size_t at, std::uint32_t v, unsigned n=4)
{ for(unsigned i=0;i<n;++i) { b[at+i]=static_cast<std::uint8_t>(v); v>>=8; } }
inline std::vector<std::uint8_t> Tga()
{
    std::vector<std::uint8_t> b{0,0,2,0,0,0,0,0,0,0,0,0,3,0,2,0,16,1,
                              0,0,1,0,0x34,0x12,0x34,0x92,0xff,0xff,0,0x80};
    return b;
}
inline std::vector<std::uint8_t> Bmp()
{
    std::vector<std::uint8_t> b(54,0); b[0]='B';b[1]='M';
    Put(b,2,72);Put(b,10,54);Put(b,14,40);Put(b,18,3);Put(b,22,2);
    Put(b,26,1,2);Put(b,28,24,2);
    // Deliberately no BMP row padding: legacy consumes tight triples.
    b.insert(b.end(),{0,0,0,8,0,0,0xa0,0x88,0x20,0xa0,0x88,0x20,255,255,255,0,0,0});
    return b;
}
inline std::vector<std::uint8_t> Wav(std::vector<std::uint8_t> pcm={0,0x80,0xff,0x7f,0xff,0xff,0,0,0x34,0x12})
{
    std::vector<std::uint8_t> b(36,0);
    // A false d prefix exercises rewind and scanning, not RIFF chunk parsing.
    b.insert(b.end(),{'d','x','d','a','t','a',0,0,0,0});Put(b,42,static_cast<std::uint32_t>(pcm.size()));
    b.insert(b.end(),pcm.begin(),pcm.end());return b;
}
}
