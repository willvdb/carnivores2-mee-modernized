// ==========================================================================
// test_menu_slider.cpp — Regression tests for discrete menu sliders
// ==========================================================================

#include <gtest/gtest.h>

#include "ListMath.h"
#include "SliderMath.h"

namespace {
constexpr int kDetailMin = 24;
constexpr int kDetailMax = 96;
constexpr int kDetailStep = 4;
constexpr int kTrackWidth = 123;
}

TEST(MenuSliderTest, ObjectDetailClampsAtEndpoints)
{
    EXPECT_EQ(DiscreteSliderValue(-0.1f, kDetailMin, kDetailMax, kDetailStep), kDetailMin);
    EXPECT_EQ(DiscreteSliderValue(1.1f, kDetailMin, kDetailMax, kDetailStep), kDetailMax);
}

TEST(MenuSliderTest, ObjectDetailMaximumHasUsableHitArea)
{
    EXPECT_EQ(DiscreteSliderValue(119.0f / kTrackWidth, kDetailMin, kDetailMax, kDetailStep), 92);
    for (int x = 120; x <= kTrackWidth; ++x) {
        EXPECT_EQ(DiscreteSliderValue(static_cast<float>(x) / kTrackWidth,
                                      kDetailMin, kDetailMax, kDetailStep),
                  kDetailMax);
    }
}

TEST(MenuListTest, VisibleRowsUseScrolledDataIndices)
{
    EXPECT_EQ(HuntListDataIndex(0, 12), 12u);
    EXPECT_EQ(HuntListDataIndex(9, 12), 21u);
    EXPECT_EQ(HuntListVisibleEnd(12, 69), 22u);
    EXPECT_EQ(HuntListVisibleEnd(64, 69), 69u);
}

TEST(MenuListTest, ScrollingClampsWithoutUnsignedWraparound)
{
    EXPECT_EQ(ScrolledHuntListOffset(0, 69, 1), 0u);
    EXPECT_EQ(ScrolledHuntListOffset(0, 69, -1), 1u);
    EXPECT_EQ(ScrolledHuntListOffset(59, 69, -1), 59u);
    EXPECT_EQ(ScrolledHuntListOffset(59, 69, 1), 58u);
    EXPECT_EQ(ScrolledHuntListOffset(0, 10, -1), 0u);
}

TEST(MenuListTest, HuntablePresentationSlotIgnoresDuplicateAiSlots)
{
    // The stock roster's AI values are not unique: Iguanodon and Carnotaurus
    // both use 17 and T-Rex uses 18. The presentation number must stay the
    // 1-based list position so each keeps its own picture and description
    // (Carnotaurus -> dino9, Tyrannosaurus Rex -> dino10).
    EXPECT_EQ(HuntableMenuSlot(7), 8u);   // Iguanodon
    EXPECT_EQ(HuntableMenuSlot(8), 9u);   // Carnotaurus, also AI 17
    EXPECT_EQ(HuntableMenuSlot(9), 10u);  // Tyrannosaurus Rex
}
