#pragma once
#include "serialized_layout_checks.h"

// Original deterministic bytes, specified independently of the production codec.
namespace ProfileGolden {
using SerializedLayout::Put32;
template<std::size_t N>
void Items(std::array<std::uint8_t, N>& b, std::size_t base, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        for (unsigned j = 0; j < 14; ++j)
            Put32(b, base + i * 56 + j * 4, 1000 + i * 100 + j);
        Put32(b, base + i * 56 + 8, 0xfffffffe); // phase = -2
        Put32(b, base + i * 56 + 32, i % 2 ? 0x80000000 : 0x3fc00000);
        Put32(b, base + i * 56 + 36, 0x42f68000); // range = 123.25
        Put32(b, base + i * 56 + 40, 0xdeadbeef); // opaque reserved bits
    }
}
inline std::array<std::uint8_t, 1516> Prefix()
{
    std::array<std::uint8_t, 1516> b{};
    for (unsigned i = 0; i < 128; ++i) b[i] = static_cast<std::uint8_t>(i + 1);
    b[7] = 0; // bytes after the terminator must survive
    Put32(b, 128, 7); Put32(b, 132, 321); Put32(b, 136, 2);
    Put32(b, 140, 123); Put32(b, 144, 45);
    Put32(b, 148, 0x3fc00000); Put32(b, 152, 0x80000000);
    Put32(b, 156, 456); Put32(b, 160, 67);
    Put32(b, 164, 0x42f68000); Put32(b, 168, 0x447a0000);
    Items(b, 172, 24);
    return b;
}
inline std::array<std::uint8_t, 1660> Save()
{
    std::array<std::uint8_t, 1660> b{};
    const auto p = Prefix();
    std::copy(p.begin(), p.end(), b.begin());
    // Ten leading options, 17 keys, nine trailing options.
    for (unsigned i = 0; i < 36; ++i) Put32(b, 1516 + i * 4, 30 + i);
    Put32(b, 1528, 5); // resolution
    Put32(b, 1532, 0xffffffff); // true, deliberately noncanonical
    Put32(b, 1544, 2); // shadows: another noncanonical true
    Put32(b, 1624, 0); // mouse invert: false
    Put32(b, 1628, 0xffffffff); Put32(b, 1632, 2);
    Put32(b, 1636, 0); Put32(b, 1640, 1);
    Put32(b, 1652, 2); Put32(b, 1656, 1);
    return b;
}
inline std::array<std::uint8_t, 7176> Room()
{
    std::array<std::uint8_t, 7176> b{};
    Put32(b, 0, 0x12345678); Put32(b, 4, 98765);
    Items(b, 8, 128);
    return b;
}

template<class Profile, class Keys>
void CheckLegacySave()
{
    const auto b = Save();
    Profile p{}; Keys k{};
    std::memcpy(&p, b.data(), 1516);
    std::memcpy(&k, b.data() + 1556, 68);
    EXPECT_EQ(p.RegNumber, 7); EXPECT_EQ(p.Score, 321);
    EXPECT_EQ(p.Last.smade, 123); EXPECT_EQ(p.Total.success, 67);
    EXPECT_FLOAT_EQ(p.Total.path, 123.25f);
    EXPECT_EQ(k.fkForward, 40); EXPECT_EQ(k.fkBinoc, 56);
    std::array<std::uint8_t, 1660> written{};
    std::memcpy(written.data(), &p, 1516);
    // The old writer emits individual 4-byte options around the native key map.
    for (unsigned i = 0; i < 10; ++i) {
        std::int32_t v; std::memcpy(&v, b.data() + 1516 + 4*i, 4);
        std::memcpy(written.data() + 1516 + 4*i, &v, 4);
    }
    std::memcpy(written.data() + 1556, &k, 68);
    std::memcpy(written.data() + 1624, b.data() + 1624, 36);
    EXPECT_EQ(written, b);
}
} // namespace ProfileGolden
