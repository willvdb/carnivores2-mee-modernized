#include <vector>
#include "../Shared/LegacyProfile.h"
#include "legacy_profile_fixtures.h"

namespace LP = LegacyProfile;
TEST(LegacyProfile, CompletePrefix)
{
    const auto b = ProfileGolden::Prefix();
    LP::Prefix p;
    ASSERT_TRUE(LP::DecodePrefix(b.data(), b.size(), p));
    EXPECT_EQ(p.header.name[127], 128);
    EXPECT_EQ(p.header.registration, 7); EXPECT_EQ(p.header.score, 321);
    EXPECT_EQ(p.last.shots, 123); EXPECT_EQ(p.last.hits, 45);
    EXPECT_EQ(p.last.path, 0x3fc00000u); EXPECT_EQ(p.last.time, 0x80000000u);
    EXPECT_EQ(p.total.shots, 456); EXPECT_EQ(p.total.hits, 67);
    EXPECT_EQ(p.total.path, 0x42f68000u); EXPECT_EQ(p.total.time, 0x447a0000u);
    for (unsigned i = 0; i < 24; ++i) {
        const auto& t = p.items[i];
        EXPECT_EQ(t.type, 1000 + i*100); EXPECT_EQ(t.weapon, 1001 + i*100);
        EXPECT_EQ(t.phase, -2); EXPECT_EQ(t.height, 1003 + i*100);
        EXPECT_EQ(t.weight, 1004 + i*100); EXPECT_EQ(t.score, 1005 + i*100);
        EXPECT_EQ(t.date, 1006 + i*100); EXPECT_EQ(t.time, 1007 + i*100);
        EXPECT_EQ(t.scale, i % 2 ? 0x80000000u : 0x3fc00000u);
        EXPECT_EQ(t.range, 0x42f68000u);
        EXPECT_EQ(static_cast<std::uint32_t>(t.reserved[0]), 0xdeadbeefu);
        for (unsigned j = 1; j < 4; ++j) EXPECT_EQ(t.reserved[j], 1010 + i*100 + j);
    }
    EXPECT_EQ(LP::EncodePrefix(p), b);
}
TEST(LegacyProfile, CompleteSave)
{
    const auto b = ProfileGolden::Save();
    LP::Save s;
    ASSERT_TRUE(LP::DecodeSave(b.data(), b.size(), s));
    EXPECT_EQ(s.options.aggression, 30); EXPECT_EQ(s.options.brightness, 39);
    EXPECT_EQ(s.options.density, 31); EXPECT_EQ(s.options.sensitivity, 32);
    EXPECT_EQ(s.options.resolution, 5); EXPECT_EQ(s.options.textures, 35);
    EXPECT_EQ(s.options.viewRange, 36); EXPECT_EQ(s.options.mouseSensitivity, 38);
    EXPECT_EQ(s.options.alphaColorKey, 62); EXPECT_EQ(s.options.system, 63);
    EXPECT_EQ(s.options.sound, 2);
    EXPECT_EQ(s.options.fog, -1); EXPECT_EQ(s.options.shadows, 2);
    EXPECT_EQ(s.options.mouseInvert, 0); EXPECT_EQ(s.options.scent, -1);
    EXPECT_EQ(s.options.camo, 2); EXPECT_EQ(s.options.radar, 0);
    EXPECT_EQ(s.options.tranq, 1); EXPECT_EQ(s.options.renderer, 1);
    for (unsigned i = 0; i < 17; ++i) EXPECT_EQ(s.options.keys[i], 40 + i);
    EXPECT_EQ(LP::EncodeSave(s), b);
}
TEST(LegacyProfile, CompleteRoom)
{
    const auto b = ProfileGolden::Room();
    LP::Room r;
    ASSERT_TRUE(LP::DecodeRoom(b.data(), b.size(), r));
    EXPECT_EQ(r.version, 0x12345678); EXPECT_EQ(r.highScore, 98765);
    for (unsigned i = 0; i < 128; ++i) {
        EXPECT_EQ(r.items[i].type, 1000 + i*100);
        EXPECT_EQ(r.items[i].phase, -2);
        EXPECT_EQ(r.items[i].reserved[3], 1013 + i*100);
    }
    EXPECT_EQ(LP::EncodeRoom(r), b);
}
TEST(LegacyProfile, TruncatedBuffersAreAtomic)
{
    const auto b = ProfileGolden::Save();
    LP::Save s; s.profile.header.score = -123;
    const auto before = LP::EncodeSave(s);
    // Use genuinely short allocations so sanitizers detect any over-read.
    for (std::size_t n : {0u, 139u, 1515u, 1516u, 1555u, 1623u, 1624u, 1659u}) {
        std::vector<std::uint8_t> shortBytes(b.begin(), b.begin() + n);
        EXPECT_FALSE(LP::DecodeSave(shortBytes.data(), n, s));
        EXPECT_EQ(LP::EncodeSave(s), before);
    }
    LP::Prefix p; p.header.score = -42;
    EXPECT_FALSE(LP::DecodePrefix(b.data(), 1515, p));
    EXPECT_EQ(p.header.score, -42);
    LP::Header h; h.rank = -3;
    EXPECT_FALSE(LP::DecodeHeader(b.data(), 139, h)); EXPECT_EQ(h.rank, -3);
    LP::Options o; o.fog = -7;
    EXPECT_FALSE(LP::DecodeOptions(b.data() + 1516, 143, o)); EXPECT_EQ(o.fog, -7);
    std::array<std::int32_t, 17> keys{}; keys.fill(-1);
    EXPECT_FALSE(LP::DecodeKeys(b.data() + 1556, 67, keys));
    for (auto k : keys) EXPECT_EQ(k, -1);
    auto rb = ProfileGolden::Room();
    LP::Room r; r.highScore = -4;
    EXPECT_FALSE(LP::DecodeRoom(rb.data(), 7175, r)); EXPECT_EQ(r.highScore, -4);
    EXPECT_FALSE(LP::DecodeRoom(nullptr, 7176, r));
    EXPECT_FALSE(LP::DecodeSave(nullptr, 1660, s));
}
TEST(LegacyProfile, ShortOptionsKeepCompleteFieldsAndAtomicKeys)
{
    const auto b = ProfileGolden::Save();
    LP::Options o; o.keys.fill(-1); o.mouseInvert = 99;
    LP::DecodeAvailableOptions(b.data() + 1516, 107, o);
    EXPECT_EQ(o.aggression, 30); EXPECT_EQ(o.brightness, 39);
    for (auto k : o.keys) EXPECT_EQ(k, -1);
    EXPECT_EQ(o.mouseInvert, 99);
    LP::DecodeAvailableOptions(b.data() + 1516, 111, o);
    EXPECT_EQ(o.keys[16], 56); EXPECT_EQ(o.mouseInvert, 99);
}
TEST(LegacyProfile, FloatBitsAreTransferredWithoutArithmetic)
{
    for (std::uint32_t bits : {0u, 0x80000000u, 0x3fc00000u, 0xc0200000u,
                               1u, 0x7f800000u, 0x7fc12345u}) {
        float value;
        LP::SetFloat(value, bits);
        EXPECT_EQ(LP::FloatBits(value), bits);
        LP::Save s; s.profile.items[23].scale = bits;
        const auto bytes = LP::EncodeSave(s);
        LP::Save decoded;
        ASSERT_TRUE(LP::DecodeSave(bytes.data(), bytes.size(), decoded));
        EXPECT_EQ(decoded.profile.items[23].scale, bits);
    }
}
