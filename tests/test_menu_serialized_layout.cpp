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
