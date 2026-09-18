#include "../Hunt/Game/DisplayPreference.h"
#include "../Hunt/Game/DisplaySelection.h"
#include <gtest/gtest.h>
#include <cstring>

namespace {
Platform::DisplayCatalog Catalog()
{
    Platform::Display secondary, primary;
    secondary.bounds = {{-1920, -200}, {1920, 1080}};
    primary.bounds = {{100, 50}, {2560, 1440}};
    secondary.modes = {{{800, 600}, 8, {144, 1}}, {{800, 600}, 32, {120, 1}},
                       {{800, 600}, 16, {240, 2}}, {{800, 600}, 32, {60000, 1001}}};
    primary.modes = {{{800, 600}, 32, {144, 1}}};
    return {{secondary, primary}, 1};
}

Platform::DisplayCatalog DuplicateSecondaries()
{
    auto catalog = Catalog();
    catalog.displays.push_back(catalog.displays[0]);
    return catalog; // Primary at index 1 has different bounds and supports 144 Hz.
}
}

TEST(DisplayParser, StrictUnsignedDecimalIncludingZeroAndUint32Maximum)
{
    std::optional<std::uint32_t> index;
    for (const auto value : {0u, 1u, 4294967295u}) {
        ASSERT_TRUE(GameDisplay::ParseDisplayIndex(std::to_string(value), index));
        EXPECT_EQ(index, value);
    }
    ASSERT_TRUE(GameDisplay::ParseDisplayIndex("0001", index));
    EXPECT_EQ(index, 1u);
}

TEST(DisplayParser, InvalidSyntaxAndOverflowClearPreviousRequest)
{
    for (const char* text : {"", "-1", "+1", "1.0", "1tail", "1e2", "0x1", " 1", "1 ",
                             "1/2", "4294967296", "999999999999999999999999999999999999"}) {
        std::optional<std::uint32_t> index = 1;
        EXPECT_FALSE(GameDisplay::ParseDisplayIndex(text, index)) << text;
        EXPECT_FALSE(index);
    }
}

TEST(DisplayParser, CaseInsensitiveDashSlashAndRepeatedLastWins)
{
    using GameDisplay::DisplayArgument;
    std::optional<std::uint32_t> index;
    for (const char* text : {"-display=0", "/display=1", "-DISPLAY=1", "/DiSpLaY=1"})
        EXPECT_EQ(GameDisplay::ApplyDisplayArgument(text, index), DisplayArgument::Applied);
    EXPECT_EQ(index, 1u);
    EXPECT_EQ(GameDisplay::ApplyDisplayArgument("-display=", index), DisplayArgument::Invalid);
    EXPECT_FALSE(index);
    EXPECT_EQ(GameDisplay::ApplyDisplayArgument("/display=2", index), DisplayArgument::Applied);
    EXPECT_EQ(index, 2u);
}

TEST(DisplayParser, ConsumesMalformedOptionsBeforeLegacySubstringHandling)
{
    std::optional<std::uint32_t> index;
    bool legacyDebug = false, legacyLanding = false;
    for (const char* text : {"-display=1-debug", "/display=x=42", "-display=y=5"}) {
        const auto parsed = GameDisplay::ApplyDisplayArgument(text, index);
        ASSERT_EQ(parsed, GameDisplay::DisplayArgument::Invalid);
        if (parsed != GameDisplay::DisplayArgument::Unrelated) continue;
        if (std::strstr(text, "-debug")) legacyDebug = true;
        if (std::strstr(text, "x=") || std::strstr(text, "y=")) legacyLanding = true;
    }
    EXPECT_FALSE(legacyDebug);
    EXPECT_FALSE(legacyLanding);
    EXPECT_FALSE(index);
}

TEST(DisplayParser, UnrelatedArgumentsAreUntouched)
{
    std::optional<std::uint32_t> index = 2;
    for (const char* text : {"", "-res=800x600", "-refresh=120", "display=1", "--display=1",
                             "-displays=1", "-display", "prj=display=1", "-windowed"}) {
        EXPECT_EQ(GameDisplay::ApplyDisplayArgument(text, index), GameDisplay::DisplayArgument::Unrelated);
        EXPECT_EQ(index, 2u);
    }
}

TEST(DisplaySelection, DefaultUsesPrimaryAtNonzeroIndexWithoutTargetRemapping)
{
    const auto selected = GameDisplay::SelectDisplay(Catalog(), std::nullopt, {800, 600}, {144, 1});
    EXPECT_EQ(selected.index, 1u);
    EXPECT_FALSE(selected.target);
    ASSERT_TRUE(selected.exclusiveMode);
    EXPECT_EQ(selected.exclusiveMode->refresh.numerator, 144u);
}

TEST(DisplaySelection, SecondaryOwnsNegativeBoundsAndFirstEligibleTargetRefresh)
{
    // The temporary catalog is gone before the returned values are accessed.
    const auto selected = GameDisplay::SelectDisplay(Catalog(), 0, {800, 600}, {240, 2});
    EXPECT_EQ(selected.index, 0u);
    ASSERT_TRUE(selected.target);
    EXPECT_TRUE(Platform::EqualDisplayBounds(selected.target->bounds, {{-1920, -200}, {1920, 1080}}));
    ASSERT_TRUE(selected.exclusiveMode);
    EXPECT_EQ(selected.exclusiveMode->bitsPerPixel, 32u);
    EXPECT_EQ(selected.exclusiveMode->refresh.numerator, 120u);
    EXPECT_EQ(selected.exclusiveMode->refresh.denominator, 1u);
}

TEST(DisplaySelection, ExplicitPrimaryAlsoGetsAnOwnedTarget)
{
    const auto selected = GameDisplay::SelectDisplay(Catalog(), 1, {800, 600});
    EXPECT_EQ(selected.index, 1u);
    ASSERT_TRUE(selected.target);
    EXPECT_EQ(selected.target->bounds.origin.x, 100);
}

TEST(DisplaySelection, InvalidIndexFallsBackToPrimaryAndItsRefreshWithoutTarget)
{
    const auto selected = GameDisplay::SelectDisplay(Catalog(), 99, {800, 600}, {144, 1});
    EXPECT_EQ(selected.index, 1u);
    EXPECT_FALSE(selected.target);
    EXPECT_EQ(selected.fallback, GameDisplay::DisplayFallback::IndexOutOfRange);
    ASSERT_TRUE(selected.exclusiveMode);
    EXPECT_EQ(selected.exclusiveMode->refresh.numerator, 144u);
}

TEST(DisplaySelection, MissingAndUnusableBoundsFallBackToPrimary)
{
    auto catalog = Catalog();
    for (const auto bounds : {std::optional<Platform::DisplayBounds>{},
                             std::optional<Platform::DisplayBounds>{{{-20, 0}, {0, 1080}}}}) {
        catalog.displays[0].bounds = bounds;
        const auto selected = GameDisplay::SelectDisplay(catalog, 0, {800, 600}, {144, 1});
        EXPECT_EQ(selected.index, 1u);
        EXPECT_FALSE(selected.target);
        EXPECT_TRUE(selected.exclusiveMode);
        EXPECT_EQ(selected.fallback, GameDisplay::DisplayFallback::MissingBounds);
    }
}

TEST(DisplaySelection, MissingPrimaryNeverSilentlySelectsSecondary)
{
    auto catalog = Catalog();
    for (const auto primary : {std::optional<Platform::DisplayIndex>{},
                              std::optional<Platform::DisplayIndex>{99}}) {
        catalog.primaryDisplay = primary;
        for (const auto requested : {std::optional<std::uint32_t>{}, std::optional<std::uint32_t>{99}}) {
            const auto selected = GameDisplay::SelectDisplay(catalog, requested, {800, 600}, {144, 1});
            EXPECT_FALSE(selected.index);
            EXPECT_FALSE(selected.target);
            EXPECT_FALSE(selected.exclusiveMode);
        }
        // A usable explicit request does not require primary metadata.
        EXPECT_TRUE(GameDisplay::SelectDisplay(catalog, 0, {800, 600}).target);
    }
    EXPECT_FALSE(GameDisplay::SelectDisplay({}, 0, {800, 600}).target);
}

TEST(DisplaySelection, LowColorAndPrimaryOnlyRefreshNeverLeakOntoTarget)
{
    const auto catalog = Catalog();
    EXPECT_TRUE(GameDisplay::SelectDisplay(catalog, 1, {800, 600}, {144, 1}).exclusiveMode);
    const auto selected = GameDisplay::SelectDisplay(catalog, 0, {800, 600}, {144, 1});
    EXPECT_TRUE(selected.target);
    EXPECT_FALSE(selected.exclusiveMode); // Target's only 144-Hz mode is 8 bpp.
    EXPECT_FALSE(GameDisplay::SelectDisplay(catalog, 0, {1024, 768}, {120, 1}).exclusiveMode);
    EXPECT_FALSE(GameDisplay::SelectDisplay(catalog, 0, {800, 600}, {}).exclusiveMode);
    EXPECT_TRUE(GameDisplay::SelectDisplay(catalog, 0, {800, 600}, {120000, 2002}).exclusiveMode);
}

TEST(DisplaySelection, EitherDuplicateSecondaryFallsBackToPrimaryWithAutomaticRefresh)
{
    for (const auto requested : {0u, 2u}) {
        SCOPED_TRACE(requested);
        const auto selected = GameDisplay::SelectDisplay(DuplicateSecondaries(), requested, {800, 600}, {144, 1});
        EXPECT_EQ(selected.index, 1u);
        EXPECT_FALSE(selected.target);
        EXPECT_FALSE(selected.exclusiveMode); // Even though primary supports 144 Hz.
        EXPECT_EQ(selected.fallback, GameDisplay::DisplayFallback::AmbiguousBounds);
    }
}

TEST(DisplaySelection, DuplicateBoundsWithoutValidPrimaryUseBackendDefault)
{
    auto catalog = DuplicateSecondaries();
    for (const auto primary : {std::optional<Platform::DisplayIndex>{},
                              std::optional<Platform::DisplayIndex>{99}}) {
        catalog.primaryDisplay = primary;
        for (const auto requested : {0u, 2u}) {
            const auto selected = GameDisplay::SelectDisplay(catalog, requested, {800, 600}, {144, 1});
            EXPECT_FALSE(selected.index);
            EXPECT_FALSE(selected.target);
            EXPECT_FALSE(selected.exclusiveMode);
            EXPECT_EQ(selected.fallback, GameDisplay::DisplayFallback::AmbiguousBounds);
        }
    }
}

TEST(DisplaySelection, DuplicatePrimaryBoundsOnlyRejectExplicitTargetRequests)
{
    auto catalog = DuplicateSecondaries();
    catalog.displays.push_back(catalog.displays[1]);
    const auto explicitPrimary = GameDisplay::SelectDisplay(catalog, 1, {800, 600}, {144, 1});
    EXPECT_EQ(explicitPrimary.index, 1u);
    EXPECT_FALSE(explicitPrimary.target);
    EXPECT_FALSE(explicitPrimary.exclusiveMode);
    EXPECT_EQ(explicitPrimary.fallback, GameDisplay::DisplayFallback::AmbiguousBounds);
    for (const auto refresh : {Platform::RefreshRate{}, Platform::RefreshRate{144, 1}}) {
        const auto normal = GameDisplay::SelectDisplay(catalog, std::nullopt, {800, 600}, refresh);
        EXPECT_EQ(normal.index, 1u);
        EXPECT_FALSE(normal.target);
        EXPECT_EQ(normal.fallback, GameDisplay::DisplayFallback::None);
        EXPECT_EQ(normal.exclusiveMode.has_value(), Platform::HasRefresh(refresh));
    }
}

TEST(DisplaySelection, FullRectangleDistinguishesTargetsAndRetainedRequestCanRecover)
{
    const std::optional<std::uint32_t> requested = 0;
    auto catalog = DuplicateSecondaries();
    EXPECT_FALSE(GameDisplay::SelectDisplay(catalog, requested, {800, 600}, {120, 1}).target);
    const auto bounds = *catalog.displays[0].bounds;
    for (const auto distinct : {Platform::DisplayBounds{{bounds.origin.x + 1, bounds.origin.y}, bounds.size},
                               Platform::DisplayBounds{{bounds.origin.x, bounds.origin.y + 1}, bounds.size},
                               Platform::DisplayBounds{bounds.origin, {bounds.size.width + 1, bounds.size.height}},
                               Platform::DisplayBounds{bounds.origin, {bounds.size.width, bounds.size.height + 1}}}) {
        catalog.displays[2].bounds = distinct;
        const auto selected = GameDisplay::SelectDisplay(catalog, requested, {800, 600}, {120, 1});
        EXPECT_EQ(selected.index, requested);
        EXPECT_EQ(selected.fallback, GameDisplay::DisplayFallback::None);
        ASSERT_TRUE(selected.target);
        EXPECT_TRUE(Platform::EqualDisplayBounds(selected.target->bounds, bounds));
        EXPECT_TRUE(selected.exclusiveMode);
    }
}
