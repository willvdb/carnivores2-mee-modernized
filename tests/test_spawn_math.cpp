#include <gtest/gtest.h>

#include <limits>

#include "SpawnMath.h"

TEST(SpawnMathTest, ZeroRatioLeaderIsSkippedForFollowerSelection)
{
    const float ratios[] = {0.0f, 1.0f};

    EXPECT_EQ(SelectWeightedRatioIndex(ratios, 2, 0.0f), 1);
    EXPECT_EQ(SelectWeightedRatioIndex(ratios, 2, 0.5f), 1);
    EXPECT_EQ(SelectWeightedRatioIndex(ratios, 2, 1.0f), 1);
}

TEST(SpawnMathTest, PositiveRatiosRetainWeightedSelection)
{
    const float ratios[] = {1.0f, 2.0f};

    EXPECT_EQ(SelectWeightedRatioIndex(ratios, 2, 0.5f), 0);
    EXPECT_EQ(SelectWeightedRatioIndex(ratios, 2, 1.5f), 1);
    EXPECT_EQ(SelectWeightedRatioIndex(ratios, 2, 3.0f), 1);
}

TEST(SpawnMathTest, AllZeroRatiosUseLegacyFirstMemberFallback)
{
    const float allZero[] = {0.0f, 0.0f};

    EXPECT_EQ(SelectWeightedRatioIndex(allZero, 2, 0.0f), 0);
}

TEST(SpawnMathTest, ZeroRatioIsValidForDisabledEntries)
{
    EXPECT_TRUE(IsValidSelectionRatio(0.0f));
    EXPECT_TRUE(IsValidSelectionRatio(0.5f));
    EXPECT_FALSE(IsValidSelectionRatio(-0.5f));
    EXPECT_FALSE(IsValidSelectionRatio(
        std::numeric_limits<float>::infinity()));
}

TEST(SpawnMathTest, InvalidRatiosCannotSelectAMember)
{
    const float negative[] = {1.0f, -1.0f};

    EXPECT_EQ(SelectWeightedRatioIndex(negative, 2, 0.5f), -1);
    EXPECT_FALSE(IsValidSelectionRatio(-1.0f));
}
