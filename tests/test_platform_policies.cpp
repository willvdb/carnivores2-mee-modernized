// Include the public surface first: this translation unit also builds on GCC
// and Clang without Windows headers or a game executable.
#include "../Hunt/Platform/Platform.h"
#include "../Hunt/Game/DisplayModes.h"
#include "../Hunt/Game/ResolutionSelection.h"
#include "../Hunt/Game/FrameTiming.h"
#ifdef _WINDOWS_
#error Portable platform headers must not include windows.h
#endif
#ifdef SDL_h_
#error Portable platform headers must not include SDL.h
#endif
#include <gtest/gtest.h>

TEST(PlatformTiming, LegacyMillisecondsWrapAt32Bits)
{
    EXPECT_EQ(Platform::WrapMilliseconds(0), 0u);
    EXPECT_EQ(Platform::WrapMilliseconds(0xffffffffULL), 0xffffffffu);
    EXPECT_EQ(Platform::WrapMilliseconds(0x100000000ULL), 0u);
    EXPECT_EQ(Platform::WrapMilliseconds(0x100000031ULL), 49u);
}
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

TEST(ResolutionSelection, ExactLookupUsesBothDimensionsAndFirstOccurrence)
{
    const Platform::Size modes[] = {{800, 480}, {640, 600}, {800, 600}, {800, 600}};
    EXPECT_EQ(GameDisplay::FindResolution(modes, 4, {800, 600}), 2);
    EXPECT_EQ(GameDisplay::FindResolution(modes, 4, {800, 480}), 0);
    EXPECT_EQ(GameDisplay::FindResolution(modes, 4, {640, 480}), -1);
    EXPECT_EQ(GameDisplay::FindResolution(modes, 4, {1920, 1080}), -1);
    EXPECT_EQ(GameDisplay::FindResolution(modes, 2, {800, 600}), -1);
    EXPECT_EQ(GameDisplay::FindResolution(nullptr, 0, {800, 600}), -1);
    EXPECT_EQ(GameDisplay::FindResolution(nullptr, -1, {800, 600}), -1);
}

TEST(ResolutionSelection, ValidLegacyOrdinalSelectsThatEntryWithoutNormalization)
{
    const Platform::Size modes[] = {{1024, 768}, {800, 600}, {640, 480}, {800, 600}};
    for (int ordinal = 0; ordinal < 4; ++ordinal) {
        const auto result = GameDisplay::ResolveLegacyResolution(modes, 4, ordinal);
        EXPECT_EQ(result.ordinal, ordinal);
        EXPECT_EQ(result.size.width, modes[ordinal].width);
        EXPECT_EQ(result.size.height, modes[ordinal].height);
    }
}

TEST(ResolutionSelection, NegativeLegacyOrdinalFallsBackToFirst800x600)
{
    const Platform::Size modes[] = {{800, 480}, {640, 600}, {800, 600}, {800, 600}};
    for (const auto ordinal : {-1, INT32_MIN}) {
        const auto result = GameDisplay::ResolveLegacyResolution(modes, 4, ordinal);
        EXPECT_EQ(result.ordinal, 2);
        EXPECT_EQ(result.size.width, 800);
        EXPECT_EQ(result.size.height, 600);
    }
}

TEST(ResolutionSelection, LegacyOrdinalAtOrPastCountFallsBackToFirst800x600)
{
    const Platform::Size modes[] = {{1024, 768}, {800, 600}, {800, 600}};
    for (const auto ordinal : {3, 4, INT32_MAX}) {
        const auto result = GameDisplay::ResolveLegacyResolution(modes, 3, ordinal);
        EXPECT_EQ(result.ordinal, 1);
        EXPECT_EQ(result.size.width, 800);
        EXPECT_EQ(result.size.height, 600);
    }
}

TEST(ResolutionSelection, InvalidLegacyOrdinalUsesIndexZeroWithout800x600)
{
    const Platform::Size modes[] = {{640, 480}, {1024, 768}, {1920, 1080}};
    for (const auto ordinal : {-1, 3}) {
        const auto result = GameDisplay::ResolveLegacyResolution(modes, 3, ordinal);
        EXPECT_EQ(result.ordinal, 0);
        EXPECT_EQ(result.size.width, 640);
        EXPECT_EQ(result.size.height, 480);
    }
}

TEST(ResolutionSelection, EmptyLegacyListUses800x600AndLeavesOrdinalUntouched)
{
    for (const auto count : {0, -1}) {
        for (const auto ordinal : {-1, 0, 17}) {
            const auto result = GameDisplay::ResolveLegacyResolution(nullptr, count, ordinal);
            EXPECT_EQ(result.ordinal, ordinal);
            EXPECT_EQ(result.size.width, 800);
            EXPECT_EQ(result.size.height, 600);
        }
    }
}

TEST(ResolutionSelection, ConfigStyleUnmatchedDimensionsYieldMinusOne)
{
    const Platform::Size modes[] = {{800, 600}, {1024, 768}};
    const Platform::Size request{1234, 567};
    const auto ordinal = GameDisplay::FindResolution(modes, 2, request);
    EXPECT_EQ(ordinal, -1); // config.cfg assigns this even when there is no match.
    EXPECT_EQ(request.width, 1234);
    EXPECT_EQ(request.height, 567);
}

TEST(ResolutionSelection, CommandLineStyleOnlySynchronizesOrdinalsOnMatch)
{
    const Platform::Size modes[] = {{800, 600}, {1024, 768}};
    // Characterize the caller's conditional assignment, including an ordinal
    // of -1 left by config and an unavailable list during the early CLI pass.
    for (const auto count : {0, 2}) {
        for (const auto previous : {-1, 0, 7}) {
            int current = 1;
            std::int32_t ordinal = previous;
            const auto index = GameDisplay::FindResolution(modes, count, {1234, 567});
            if (index >= 0) current = ordinal = index;
            EXPECT_EQ(current, 1);
            EXPECT_EQ(ordinal, previous);
        }
    }
    int current = 0;
    std::int32_t ordinal = -1;
    const auto index = GameDisplay::FindResolution(modes, 2, {1024, 768});
    if (index >= 0) current = ordinal = index;
    EXPECT_EQ(current, 1);
    EXPECT_EQ(ordinal, 1);
}

TEST(ResolutionSelection, ProfileThenConfigThenCommandLineDimensionPrecedence)
{
    // Policy composition only: the engine retains its existing parsing and
    // startup order. No game, profile I/O, or window backend is needed here.
    const Platform::Size modes[] = {{800, 600}, {1024, 768}, {1920, 1080}};
    auto selected = GameDisplay::ResolveLegacyResolution(modes, 3, 1);
    EXPECT_EQ(selected.size.width, 1024);
    EXPECT_EQ(selected.size.height, 768);
    EXPECT_EQ(selected.ordinal, 1);

    selected.size = {1920, 1080}; // config dimension override
    selected.ordinal = GameDisplay::FindResolution(modes, 3, selected.size);
    EXPECT_EQ(selected.size.width, 1920);
    EXPECT_EQ(selected.size.height, 1080);
    EXPECT_EQ(selected.ordinal, 2);

    selected.size = {1234, 567}; // unmatched command-line override
    const auto index = GameDisplay::FindResolution(modes, 3, selected.size);
    if (index >= 0) selected.ordinal = index;
    EXPECT_EQ(selected.size.width, 1234);
    EXPECT_EQ(selected.size.height, 567);
    EXPECT_EQ(selected.ordinal, 2);
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
