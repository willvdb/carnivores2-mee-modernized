// Include the public surface first: this translation unit also builds on GCC
// and Clang without Windows headers or a game executable.
#include "../Hunt/Platform/Platform.h"
#include "../Hunt/Game/DisplayModes.h"
#include "../Hunt/Game/FrameTiming.h"
#ifdef _WINDOWS_
#error Portable platform headers must not include windows.h
#endif
#include <gtest/gtest.h>
#include <type_traits>

static_assert(sizeof(Platform::Tick) == 8);
static_assert(sizeof(Platform::KeyboardState) == 256);
static_assert(std::is_same_v<std::remove_extent_t<Platform::KeyboardState>, std::uint8_t>);

TEST(PlatformDisplay, FiltersDepthAndEitherDesktopDimensionWithoutSorting)
{
    Platform::DisplayInfo display{{1920, 1080}, {
        {{1024, 768}, 32}, {{800, 600}, 8}, {{2048, 768}, 32},
        {{1024, 1200}, 32}, {{640, 480}, 16}, {{1280, 720}, 24}}};
    const auto result = GameDisplay::SelectResolutions(display);
    ASSERT_EQ(result.count, 4);
    EXPECT_EQ(result.modes[0].width, 1024);
    EXPECT_EQ(result.modes[1].width, 640); // No new 800x600 dimension minimum.
    EXPECT_EQ(result.modes[2].width, 1280);
    EXPECT_EQ(result.modes[3].width, 1920);
    EXPECT_EQ(result.modes[3].height, 1080);
}

TEST(PlatformDisplay, DeduplicatesDimensionsAcrossDepthsAndDesktopAppend)
{
    Platform::DisplayInfo display{{1920, 1080}, {
        {{800, 600}, 16}, {{1920, 1080}, 32}, {{800, 600}, 32},
        {{800, 480}, 32}, {{1920, 1080}, 16}}};
    const auto result = GameDisplay::SelectResolutions(display);
    ASSERT_EQ(result.count, 3);
    EXPECT_EQ(result.modes[0].height, 600);
    EXPECT_EQ(result.modes[1].width, 1920);
    EXPECT_EQ(result.modes[2].height, 480);
}

TEST(PlatformDisplay, IncludesDesktopWhenEnumerationIsEmptyOrUnusable)
{
    for (const auto& modes : {std::vector<Platform::DisplayMode>{},
         std::vector<Platform::DisplayMode>{{{2560, 1440}, 8}, {{3840, 2160}, 32}}}) {
        const auto result = GameDisplay::SelectResolutions({{2560, 1440}, modes});
        ASSERT_EQ(result.count, 1);
        EXPECT_EQ(result.modes[0].width, 2560);
        EXPECT_EQ(result.modes[0].height, 1440);
    }
}

TEST(PlatformDisplay, KeepsHistoricalFallbackForEmptyList)
{
    GameDisplay::Resolutions empty;
    empty.EnsureFallback();
    ASSERT_EQ(empty.count, 1);
    EXPECT_EQ(empty.modes[0].width, 800);
    EXPECT_EQ(empty.modes[0].height, 600);
    empty.EnsureFallback();
    EXPECT_EQ(empty.count, 1);

    GameDisplay::Resolutions existing;
    existing.Add({640, 480});
    existing.EnsureFallback();
    ASSERT_EQ(existing.count, 1);
    EXPECT_EQ(existing.modes[0].width, 640);
}

TEST(PlatformDisplay, DesktopUsesLastAvailableSlot)
{
    Platform::DisplayInfo display{{1920, 1080}, {}};
    for (int i = 0; i < 127; ++i) display.modes.push_back({{800 + i, 600}, 32});
    const auto result = GameDisplay::SelectResolutions(display);
    ASSERT_EQ(result.count, 128);
    EXPECT_EQ(result.modes[127].width, 1920);
    EXPECT_EQ(result.modes[127].height, 1080);
}

TEST(PlatformDisplay, FullListRetainsFirst128WithoutEvictingForDesktop)
{
    Platform::DisplayInfo display{{1920, 1080}, {}};
    for (int i = 0; i < 140; ++i) {
        display.modes.push_back({{800 + i, 600}, 32});
        display.modes.push_back({{800 + i, 600}, 16});
    }
    const auto result = GameDisplay::SelectResolutions(display);
    ASSERT_EQ(result.count, 128);
    for (int i = 0; i < 128; ++i) EXPECT_EQ(result.modes[i].width, 800 + i);
}

TEST(PlatformDisplay, DoesNotInventValidationForUnusualDesktopDimensions)
{
    // The old unconditional desktop append also accepted this backend failure
    // value. Fixing it would be a distinct policy change, not seam extraction.
    const auto result = GameDisplay::SelectResolutions({{0, 0}, {}});
    ASSERT_EQ(result.count, 1);
    EXPECT_EQ(result.modes[0].width, 0);
    EXPECT_EQ(result.modes[0].height, 0);
}

TEST(PlatformTiming, PreservesMenuIndicesAndIntegerTargetIntervals)
{
    EXPECT_EQ(FrameTiming::TargetMicroseconds(0), 0);
    EXPECT_EQ(FrameTiming::TargetMicroseconds(1), 16666);
    EXPECT_EQ(FrameTiming::TargetMicroseconds(2), 8333);
    EXPECT_EQ(FrameTiming::TargetMicroseconds(3), 4166);
}

TEST(PlatformTiming, UsesCounterDifferencesAndTruncatesFractionalMicroseconds)
{
    constexpr std::int64_t start = INT64_C(5000000000000);
    EXPECT_EQ(FrameTiming::ElapsedMicroseconds(start, start, 10000000), 0);
    EXPECT_EQ(FrameTiming::ElapsedMicroseconds(start, start + 166659, 10000000), 16665);
    EXPECT_EQ(FrameTiming::ElapsedMicroseconds(start, start + 166660, 10000000), 16666);
    EXPECT_EQ(FrameTiming::ElapsedMicroseconds(start, start + 1, 3000000), 0);
    EXPECT_EQ(FrameTiming::ElapsedMicroseconds(start, start + 6, 3000000), 2);
}
