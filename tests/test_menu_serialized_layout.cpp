#include <gtest/gtest.h>
#include "../Menu/Hunt.h"
#include "serialized_layout_checks.h"

static_assert(SerializedLayout::CheckProfile<Profile, ProfileStats, TrophyItem, TKeyMap>());
RECORD_OFFSET(Profile, Name, 0);
RECORD_OFFSET(TrophyItem, m_CType, 0);
RECORD_OFFSET(TrophyItem, m_Weapon, 4);
RECORD_OFFSET(TrophyItem, m_Phase, 8);
RECORD_OFFSET(TrophyItem, m_Height, 12);
RECORD_OFFSET(TrophyItem, m_Weight, 16);
RECORD_OFFSET(TrophyItem, m_Score, 20);
RECORD_OFFSET(TrophyItem, m_Date, 24);
RECORD_OFFSET(TrophyItem, m_Time, 28);
RECORD_OFFSET(TrophyItem, m_Scale, 32);
RECORD_OFFSET(TrophyItem, m_Range, 36);
RECORD_OFFSET(TrophyItem, m_Reserved, 40);
RECORD_SIZE(TARGAINFOHEADER, 18);
RECORD_OFFSET(TARGAINFOHEADER, tgaIdentSize, 0);
RECORD_OFFSET(TARGAINFOHEADER, tgaColorMapType, 1);
RECORD_OFFSET(TARGAINFOHEADER, tgaImageType, 2);
RECORD_OFFSET(TARGAINFOHEADER, tgaColorMapOffset, 3);
RECORD_OFFSET(TARGAINFOHEADER, tgaColorMapLength, 5);
RECORD_OFFSET(TARGAINFOHEADER, tgaColorMapBits, 7);
RECORD_OFFSET(TARGAINFOHEADER, tgaXStart, 8);
RECORD_OFFSET(TARGAINFOHEADER, tgaYStart, 10);
RECORD_OFFSET(TARGAINFOHEADER, tgaWidth, 12);
RECORD_OFFSET(TARGAINFOHEADER, tgaHeight, 14);
RECORD_OFFSET(TARGAINFOHEADER, tgaBits, 16);
RECORD_OFFSET(TARGAINFOHEADER, tgaDescriptor, 17);

TEST(SerializedLayout, MenuProfileMatchesLegacyBytes)
{
    const auto profile = SerializedLayout::CheckProfileBytes<Profile>();
    EXPECT_STREQ(profile.Name, "T");
    EXPECT_EQ(profile.Body[0].m_CType, 10);
    EXPECT_FLOAT_EQ(profile.Body[0].m_Scale, 2.0f);
    EXPECT_EQ(profile.Body[23].m_Reserved[3], 0x76543210);
}

#include "legacy_profile_fixtures.h"
TEST(SerializedLayout, CompleteLegacyProfileFixture)
{
    ProfileGolden::CheckLegacySave<Profile, TKeyMap>();
}

#include "../Menu/ProfileSerialization.h"
TEST(MenuProfile, SameCompleteFixtureAndCanonicalBooleans)
{
    auto b = ProfileGolden::Save();
    Profile p{}; Options o{};
    ASSERT_TRUE(MenuProfile::LoadProfile(b.data(), b.size(), p, o));
    EXPECT_EQ(std::memcmp(&p, b.data(), 1516), 0);
    EXPECT_EQ(p.Body[23].m_Phase, -2);
    EXPECT_EQ(o.Aggression, 30); EXPECT_EQ(o.Density, 31); EXPECT_EQ(o.Sensitivity, 32);
    EXPECT_EQ(o.Resolution, 5); EXPECT_EQ(o.Textures, 35); EXPECT_EQ(o.ViewRange, 36);
    EXPECT_EQ(o.MouseSensitivity, 38); EXPECT_EQ(o.Brightness, 39);
    EXPECT_EQ(o.AlphaColorKey, 62); EXPECT_EQ(o.OptSys, 63);
    EXPECT_EQ(o.SoundAPI, 2); EXPECT_EQ(o.RenderAPI, 1);
    EXPECT_EQ(o.KeyMap.fkForward, 40); EXPECT_EQ(o.KeyMap.fkBinoc, 56);
    EXPECT_EQ(std::memcmp(&o.KeyMap, b.data() + 1556, 68), 0);
    EXPECT_TRUE(o.Fog); EXPECT_TRUE(o.Shadows); EXPECT_FALSE(o.MouseInvert);
    EXPECT_TRUE(o.ScentMode); EXPECT_TRUE(o.CamoMode);
    EXPECT_FALSE(o.RadarMode); EXPECT_TRUE(o.TranqMode);
    // Menu always writes true as a four-byte 1, including equipment.
    for (unsigned offset : {1532u, 1544u, 1628u, 1632u}) ProfileGolden::Put32(b, offset, 1);
    EXPECT_EQ(LegacyProfile::EncodeSave({MenuProfile::FromRuntime(p), MenuProfile::CaptureOptions(o)}), b);
    ASSERT_TRUE(MenuProfile::LoadProfile(b.data(), b.size(), p, o));
    EXPECT_EQ(LegacyProfile::EncodeSave({MenuProfile::FromRuntime(p), MenuProfile::CaptureOptions(o)}), b);
}
TEST(MenuProfile, TruncationDoesNotApplyPartialProfileOrOptions)
{
    auto b = ProfileGolden::Save();
    Profile p{}; Options o{};
    p.Score = -321; o.Aggression = 999;
    for (unsigned n : {139u, 1515u, 1516u, 1555u, 1623u, 1659u}) {
        EXPECT_FALSE(MenuProfile::LoadProfile(b.data(), n, p, o));
        EXPECT_EQ(p.Score, -321); EXPECT_EQ(o.Aggression, 999);
    }
}
TEST(MenuProfile, ProfileListHeaderHasBoundedNameAndFixedFields)
{
    auto b = ProfileGolden::Save();
    LegacyProfile::Header h;
    ASSERT_TRUE(LegacyProfile::DecodeHeader(b.data(), 140, h));
    EXPECT_EQ(h.name[127], 128); EXPECT_EQ(h.registration, 7);
    EXPECT_EQ(h.score, 321); EXPECT_EQ(h.rank, 2);
}

TEST(MenuProfile, LoadAndSaveRegenerateRankIncludingMasterOverride)
{
    for (auto score : {-1, 0, 99, 100, 299, 300, 9999, 10000}) {
        Profile p{}; p.Score = score; p.Rank = -99;
        MenuProfile::UpdateRank(p);
        EXPECT_EQ(p.Rank, score >= 10000 ? 1000 : score >= 300 ? 2 : score >= 100 ? 1 : 0);
    }
}
