// test_water_color.cpp
// Unit tests for Hunt/Core/WaterColor.h -- the depth tint applied to water and
// fog colour while submerged.
//
// The comment in Controls.cpp asserts that fogRGB is packed BGR (bits 0-7
// blue, 8-15 green, 16-23 red) and that the tint preserves the water's own
// hue: blue ocean deepens to dark blue, brown swamp water to dark brown. None
// of that was checked, so a channel swap or a return to the old fixed
// blue-biased curve would pass unnoticed. The header is pure
// (<cstdint>/<algorithm>/<cmath>), so no engine linkage is needed.

#include <gtest/gtest.h>

#include <cmath>

#include "Core/WaterColor.h"

namespace {

constexpr int kBlue(int packed) { return packed & 0xFF; }
constexpr int kGreen(int packed) { return (packed >> 8) & 0xFF; }
constexpr int kRed(int packed) { return (packed >> 16) & 0xFF; }
constexpr int kPack(int r, int g, int b) { return (r << 16) | (g << 8) | b; }

TEST(WaterColor, SurfaceAndAboveLeaveTheColourAlone) {
    int r = 139, g = 69, b = 19;
    ModulateWaterColorByDepth(r, g, b, 0.0f);
    EXPECT_EQ(r, 139);
    EXPECT_EQ(g, 69);
    EXPECT_EQ(b, 19);

    ModulateWaterColorByDepth(r, g, b, -0.5f);
    EXPECT_EQ(r, 139);

    EXPECT_EQ(ModulateWaterColorByDepthBGR(0x8B4513, 0.0f), 0x8B4513);
}

TEST(WaterColor, BlackWaterStaysBlack) {
    int r = 0, g = 0, b = 0;
    ModulateWaterColorByDepth(r, g, b, 1.0f);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(g, 0);
    EXPECT_EQ(b, 0);
}

TEST(WaterColor, DominantChannelFadesToAboutTwoThirds) {
    // All three channels equal means all three are dominant, so all three
    // should fade by exp(-kFloor) = exp(-0.40) ~= 0.670.
    int r = 255, g = 255, b = 255;
    ModulateWaterColorByDepth(r, g, b, 1.0f);

    const int expected = static_cast<int>(255.0f * std::exp(-0.40f) + 0.5f);
    EXPECT_EQ(r, expected);
    EXPECT_EQ(g, expected);
    EXPECT_EQ(b, expected);
    EXPECT_NEAR(expected / 255.0, 0.670f, 0.01f);
}

TEST(WaterColor, BlueOceanDeepensToBlueAndKeepsBgrOrder) {
    // Pure blue at full depth: blue is dominant, so it survives while red
    // and green are absorbed to the floor.
    const int packed = ModulateWaterColorByDepthBGR(kPack(0, 0, 255), 1.0f);

    EXPECT_GT(kBlue(packed), kRed(packed));
    EXPECT_GT(kBlue(packed), kGreen(packed));
    // A channel swap would put the surviving red in bits 0-7 instead.
    EXPECT_GT(kBlue(packed), 100);
    EXPECT_LE(kRed(packed), 2);
    EXPECT_LE(kGreen(packed), 2);
}

TEST(WaterColor, RedLandsInTheHighBitsNotTheLowOnes) {
    // Guards the packing itself: a red-only colour must come back with the
    // surviving value in bits 16-23.
    const int packed = ModulateWaterColorByDepthBGR(kPack(255, 0, 0), 1.0f);

    EXPECT_GT(kRed(packed), 100);
    EXPECT_LE(kBlue(packed), 2);
    EXPECT_LE(kGreen(packed), 2);
}

TEST(WaterColor, BrownSwampWaterDeepensToBrown) {
    // The old fixed blue-biased curve drove this toward navy. Red is the
    // dominant channel here, so red must still dominate after the tint.
    const int packed = ModulateWaterColorByDepthBGR(kPack(139, 69, 19), 1.0f);

    EXPECT_GT(kRed(packed), kGreen(packed));
    EXPECT_GT(kRed(packed), kBlue(packed));
}

TEST(WaterColor, ChannelsNeverFadeToZero) {
    // A pure-black channel would read as a rendering fault, so the floor is 2.
    int r = 255, g = 1, b = 1;
    ModulateWaterColorByDepth(r, g, b, 1.0f);

    EXPECT_GE(g, 2);
    EXPECT_GE(b, 2);
}

TEST(WaterColor, DepthIsClampedToOne) {
    int r1 = 139, g1 = 69, b1 = 19;
    int r2 = 139, g2 = 69, b2 = 19;
    ModulateWaterColorByDepth(r1, g1, b1, 1.0f);
    ModulateWaterColorByDepth(r2, g2, b2, 5.0f);

    EXPECT_EQ(r1, r2);
    EXPECT_EQ(g1, g2);
    EXPECT_EQ(b1, b2);
}

}  // namespace
