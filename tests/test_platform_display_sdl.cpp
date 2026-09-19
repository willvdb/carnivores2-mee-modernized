#include "../Hunt/Platform/PlatformSDLInternal.h"
#include "../Hunt/Game/DisplayModes.h"
#include "../Hunt/Game/RefreshSelection.h"
#include "../Hunt/Game/DisplaySelection.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <memory>
#include <utility>

namespace {
void ExpectMode(const Platform::DisplayMode& actual, const SDL_DisplayMode& native)
{
    EXPECT_EQ(actual.size.width, native.w);
    EXPECT_EQ(actual.size.height, native.h);
    EXPECT_EQ(actual.bitsPerPixel, static_cast<std::uint32_t>(SDL_BITSPERPIXEL(native.format)));
    const bool known = native.refresh_rate_numerator > 0 && native.refresh_rate_denominator > 0;
    EXPECT_EQ(actual.refresh.numerator, known ? native.refresh_rate_numerator : 0);
    EXPECT_EQ(actual.refresh.denominator, known ? native.refresh_rate_denominator : 0);
}
void ExpectModes(const Platform::DisplayInfo& actual, const Platform::DisplayInfo& expected)
{
    EXPECT_EQ(actual.desktop.width, expected.desktop.width);
    EXPECT_EQ(actual.desktop.height, expected.desktop.height);
    ASSERT_EQ(actual.modes.size(), expected.modes.size());
    for (std::size_t i = 0; i < actual.modes.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(actual.modes[i].size.width, expected.modes[i].size.width);
        EXPECT_EQ(actual.modes[i].size.height, expected.modes[i].size.height);
        EXPECT_EQ(actual.modes[i].bitsPerPixel, expected.modes[i].bitsPerPixel);
        EXPECT_EQ(actual.modes[i].refresh.numerator, expected.modes[i].refresh.numerator);
        EXPECT_EQ(actual.modes[i].refresh.denominator, expected.modes[i].refresh.denominator);
    }
}
}

TEST(SDLDisplayConversion, KeepsExactRationalInsteadOfRoundedFloat)
{
    SDL_DisplayMode mode{};
    mode.w = 1920; mode.h = 1080; mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.refresh_rate = 59.94f;
    mode.refresh_rate_numerator = 60000; mode.refresh_rate_denominator = 1001;
    ExpectMode(Platform::SDLDetails::CopyDisplayMode(mode), mode);
    mode.refresh_rate_numerator = 60; mode.refresh_rate_denominator = 1;
    ExpectMode(Platform::SDLDetails::CopyDisplayMode(mode), mode);
    mode.refresh_rate_numerator = 120000; mode.refresh_rate_denominator = 2002;
    ExpectMode(Platform::SDLDetails::CopyDisplayMode(mode), mode);
}

TEST(SDLDisplayConversion, InvalidAndUnspecifiedFractionsDoNotBecomeHugeUnsignedRates)
{
    for (int numerator : {0, -1, 60}) {
        for (int denominator : {0, -1, 1}) {
            SDL_DisplayMode mode{};
            mode.refresh_rate_numerator = numerator;
            mode.refresh_rate_denominator = denominator;
            ExpectMode(Platform::SDLDetails::CopyDisplayMode(mode), mode);
        }
    }
}

TEST(SDLRefreshMapping, UsesCallerSelectedDepthAndExactRationalsInBackendOrder)
{
    SDL_DisplayMode low{}, wrongSize{}, integer{}, duplicate{}, fractional{};
    integer.displayID = 17; integer.w = 800; integer.h = 600; integer.format = SDL_PIXELFORMAT_RGB565;
    integer.refresh_rate = 59.0f; // The convenience float must not drive selection.
    integer.refresh_rate_numerator = 60000; integer.refresh_rate_denominator = 1000;
    low = integer; low.format = SDL_PIXELFORMAT_INDEX8;
    wrongSize = integer; wrongSize.h = 768;
    duplicate = integer; duplicate.format = SDL_PIXELFORMAT_XRGB8888;
    duplicate.refresh_rate_numerator = 60; duplicate.refresh_rate_denominator = 1;
    fractional = duplicate; fractional.refresh_rate_denominator = 1001;
    fractional.refresh_rate_numerator = 60000;
    SDL_DisplayMode* modes[] = {&low, &wrongSize, &integer, &duplicate, &fractional};
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 5, {{800, 600}, 16, {60, 1}}, 17), &integer);
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 5, {{800, 600}, 16, {120, 2}}, 17), &integer);
    // XRGB8888 has 24 color bits in SDL's discovery metadata, despite 32-bit storage.
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 5, {{800, 600}, 24, {60, 1}}, 17), &duplicate);
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 5, {{800, 600}, 24, {60000, 1001}}, 17), &fractional);
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 5, {{800, 600}, 24, {5994, 100}}, 17), nullptr);
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 5, {{800, 600}, 24, {144, 1}}, 17), nullptr);
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 5, {{800, 600}, 16, {}}, 17), nullptr);
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(nullptr, 0, {{800, 600}, 16, {60, 1}}, 17), nullptr);
    // Mapping itself does not impose the engine's minimum-color policy.
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 5, {{800, 600}, 8, {60, 1}}, 17), &low);
}

TEST(SDLRefreshMapping, EngineSelectedModeDisappearingReturnsNoNativeMatch)
{
    SDL_DisplayMode low{}, usable{}, duplicate{};
    usable.displayID = 17; usable.w = 800; usable.h = 600; usable.format = SDL_PIXELFORMAT_RGB565;
    usable.refresh_rate_numerator = 60000; usable.refresh_rate_denominator = 1000;
    duplicate = usable;
    low = usable; low.format = SDL_PIXELFORMAT_INDEX8;
    Platform::Display primary;
    primary.modes = {Platform::SDLDetails::CopyDisplayMode(low), Platform::SDLDetails::CopyDisplayMode(usable),
                     Platform::SDLDetails::CopyDisplayMode(duplicate)};
    const auto selected = GameDisplay::SelectPrimaryRefreshMode({{primary}, 0}, {800, 600}, {60, 1});
    ASSERT_TRUE(selected);
    SDL_DisplayMode* modes[] = {&low, &usable, &duplicate};
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 3, *selected, 17), &usable);
    // All selected-depth modes disappear between discovery and application.
    // Return no match (automatic fallback), never remap to the rejected 8-bpp mode.
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 1, *selected, 17), nullptr);
}

TEST(SDLTargetMapping, MatchesFullNegativeRectangleRatherThanDimensionsOrEnumerationPosition)
{
    const Platform::DisplayTarget target{{{-1920, -200}, {1920, 1080}}};
    const Platform::DisplayMode mode{{800, 600}, 16, {60, 1}};
    const std::vector<Platform::SDLDetails::NativeDisplay> displays{
        {42, {{0, 0}, {1920, 1080}}}, {99, target.bounds}};
    const auto mapped = Platform::SDLDetails::MapWindowDisplay(displays, 42, target, mode);
    EXPECT_EQ(mapped.id, 99u);
    ASSERT_TRUE(mapped.target);
    EXPECT_TRUE(Platform::EqualDisplayBounds(mapped.target->bounds, target.bounds));
    EXPECT_TRUE(mapped.exclusiveMode);
    EXPECT_FALSE(mapped.ambiguousBounds);
    for (const auto wrong : {Platform::DisplayBounds{{-1920, -201}, {1920, 1080}},
                            Platform::DisplayBounds{{-1921, -200}, {1920, 1080}},
                            Platform::DisplayBounds{{-1920, -200}, {1919, 1080}},
                            Platform::DisplayBounds{{-1920, -200}, {1920, 1079}}}) {
        const auto fallback = Platform::SDLDetails::MapWindowDisplay(displays, 42, Platform::DisplayTarget{wrong}, mode);
        EXPECT_EQ(fallback.id, 42u);
        EXPECT_FALSE(fallback.target);
        EXPECT_FALSE(fallback.exclusiveMode);
        EXPECT_FALSE(fallback.ambiguousBounds);
    }
}

TEST(SDLTargetMapping, DisappearedTargetUsesPrimaryAndDiscardsRefresh)
{
    const Platform::DisplayTarget gone{{{-1920, 0}, {1920, 1080}}};
    const Platform::DisplayMode mode{{800, 600}, 16, {120, 1}};
    const auto mapped = Platform::SDLDetails::MapWindowDisplay(
        {{88, {{1920, 0}, {1920, 1080}}}, {42, {{0, 0}, {1920, 1080}}}}, 42, gone, mode);
    EXPECT_EQ(mapped.id, 42u);
    EXPECT_FALSE(mapped.target);
    EXPECT_FALSE(mapped.exclusiveMode);
    EXPECT_FALSE(mapped.ambiguousBounds);
    const auto automatic = Platform::SDLDetails::MapWindowDisplay({}, 42, std::nullopt, std::nullopt);
    EXPECT_EQ(automatic.id, 42u);
    EXPECT_FALSE(automatic.exclusiveMode);
    const auto primaryRefresh = Platform::SDLDetails::MapWindowDisplay({}, 42, std::nullopt, mode);
    EXPECT_EQ(primaryRefresh.id, 42u);
    EXPECT_TRUE(primaryRefresh.exclusiveMode);
}

TEST(SDLTargetMapping, TargetBecomingDuplicatedUsesPrimaryAndDiscardsTargetAndRefresh)
{
    Platform::Display primary, secondary;
    primary.bounds = {{100, 50}, {2560, 1440}};
    secondary.bounds = {{-1920, -200}, {1920, 1080}};
    primary.modes = secondary.modes = {{{800, 600}, 16, {120, 1}}};
    const auto selected = GameDisplay::SelectDisplay({{secondary, primary}, 1}, 0, {800, 600}, {120, 1});
    ASSERT_TRUE(selected.target);
    ASSERT_TRUE(selected.exclusiveMode);
    std::vector<Platform::SDLDetails::NativeDisplay> displays{
        {99, *secondary.bounds}, {42, *primary.bounds}};
    const auto unique = Platform::SDLDetails::MapWindowDisplay(displays, 42, selected.target, selected.exclusiveMode);
    EXPECT_EQ(unique.id, 99u);
    EXPECT_TRUE(unique.target);
    EXPECT_TRUE(unique.exclusiveMode);
    EXPECT_FALSE(unique.ambiguousBounds);
    // Native topology changes after the engine snapshot. Never choose 99 or 88.
    displays.push_back({88, *secondary.bounds});
    for (int i = 0; i < 2; ++i) {
        const auto mapped = Platform::SDLDetails::MapWindowDisplay(displays, 42, selected.target, selected.exclusiveMode);
        EXPECT_EQ(mapped.id, 42u);
        EXPECT_FALSE(mapped.target);
        EXPECT_FALSE(mapped.exclusiveMode);
        EXPECT_TRUE(mapped.ambiguousBounds);
        std::swap(displays.front(), displays.back());
    }
}

TEST(SDLTargetMapping, NoTargetPreservesPrimaryModeEvenWithDuplicateBounds)
{
    const Platform::DisplayBounds bounds{{-1920, -200}, {1920, 1080}};
    const Platform::DisplayMode mode{{800, 600}, 16, {120, 1}};
    const std::vector<Platform::SDLDetails::NativeDisplay> displays{{99, bounds}, {42, bounds}, {88, bounds}};
    const auto mapped = Platform::SDLDetails::MapWindowDisplay(displays, 42, std::nullopt, mode);
    EXPECT_EQ(mapped.id, 42u);
    EXPECT_FALSE(mapped.target);
    ASSERT_TRUE(mapped.exclusiveMode);
    EXPECT_FALSE(mapped.ambiguousBounds);
    EXPECT_TRUE(Platform::EqualRefresh(mapped.exclusiveMode->refresh, mode.refresh));
    const auto automatic = Platform::SDLDetails::MapWindowDisplay(displays, 42, std::nullopt, std::nullopt);
    EXPECT_EQ(automatic.id, 42u);
    EXPECT_FALSE(automatic.target);
    EXPECT_FALSE(automatic.exclusiveMode);
}

TEST(SDLTargetMapping, ExactModeNeverMapsToAnotherDisplayEvenIfItsFieldsMatch)
{
    SDL_DisplayMode primary{};
    primary.displayID = 42; primary.w = 800; primary.h = 600;
    primary.format = SDL_PIXELFORMAT_RGB565;
    primary.refresh_rate_numerator = 120; primary.refresh_rate_denominator = 1;
    auto secondary = primary; secondary.displayID = 99;
    SDL_DisplayMode* modes[] = {&primary, &secondary};
    const Platform::DisplayMode selected{{800, 600}, 16, {240, 2}};
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 2, selected, 99), &secondary);
    // Removing the target's mode must not pick identical primary fields.
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 1, selected, 99), nullptr);
    EXPECT_EQ(Platform::SDLDetails::FindNativeDisplayMode(modes, 2, selected, 123), nullptr);
}

namespace {
SDL_DisplayMode automaticResult{};
bool automaticAvailable = true;
bool SDLCALL QueryAutomatic(SDL_DisplayID display, int w, int h, float rate, bool density, SDL_DisplayMode* result)
{
    EXPECT_EQ(display, 99u);
    EXPECT_EQ(w, 800);
    EXPECT_EQ(h, 600);
    EXPECT_EQ(rate, 0.0f);
    EXPECT_FALSE(density);
    *result = automaticResult;
    return automaticAvailable;
}
}

TEST(SDLTargetMapping, AutomaticUsesZeroRefreshOnTargetAndRejectsCloseOrForeignModes)
{
    SDL_DisplayMode result{};
    automaticResult = {};
    automaticResult.displayID = 99; automaticResult.w = 800; automaticResult.h = 600;
    automaticAvailable = true;
    EXPECT_TRUE(Platform::SDLDetails::FindAutomaticDisplayMode(99, {800, 600}, result, QueryAutomatic));
    automaticResult.w = 1024;
    EXPECT_FALSE(Platform::SDLDetails::FindAutomaticDisplayMode(99, {800, 600}, result, QueryAutomatic));
    automaticResult.w = 800; automaticResult.h = 768;
    EXPECT_FALSE(Platform::SDLDetails::FindAutomaticDisplayMode(99, {800, 600}, result, QueryAutomatic));
    automaticResult.h = 600; automaticResult.displayID = 42;
    EXPECT_FALSE(Platform::SDLDetails::FindAutomaticDisplayMode(99, {800, 600}, result, QueryAutomatic));
    automaticResult.displayID = 99; automaticAvailable = false;
    EXPECT_FALSE(Platform::SDLDetails::FindAutomaticDisplayMode(99, {800, 600}, result, QueryAutomatic));
}

class SDLDisplayCatalog : public ::testing::Test {
protected:
    void SetUp() override {
        // Headless CI is deterministic; optionally probe a real video driver
        // with this test alone. Neither path creates a window or changes modes.
        const char* driver = std::getenv("CARNIVORES_TEST_DISPLAY_DRIVER");
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, driver ? driver : "dummy"));
        ASSERT_TRUE(Platform::InitializeApplication()) << Platform::LastError();
    }
    void TearDown() override { Platform::ShutdownApplication(); }
};

TEST_F(SDLDisplayCatalog, NonWindowsDriverDoesNotBorrowHostMonitorIdentity)
{
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "windows") == 0 ||
        SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0 ||
        SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
        GTEST_SKIP() << "Native discovery is covered separately";
    const auto catalog = Platform::QueryDisplayCatalog();
    ASSERT_FALSE(catalog.displays.empty());
    for (const auto& display : catalog.displays) EXPECT_FALSE(display.identity);
}

TEST_F(SDLDisplayCatalog, SnapshotCopiesAllBackendDisplaysModesAndBounds)
{
    const auto catalog = Platform::QueryDisplayCatalog();
    int count = 0;
    const std::unique_ptr<SDL_DisplayID, decltype(&SDL_free)> ids(SDL_GetDisplays(&count), SDL_free);
    ASSERT_NE(ids, nullptr);
    ASSERT_GT(count, 0);
    ASSERT_EQ(catalog.displays.size(), static_cast<std::size_t>(count));
    ASSERT_TRUE(catalog.primaryDisplay.has_value());
    ASSERT_LT(*catalog.primaryDisplay, catalog.displays.size());
    EXPECT_EQ(ids.get()[*catalog.primaryDisplay], SDL_GetPrimaryDisplay());
    for (int i = 0; i < count; ++i) {
        SCOPED_TRACE(i);
        const auto& display = catalog.displays[i];
        SDL_Rect bounds{};
        ASSERT_TRUE(SDL_GetDisplayBounds(ids.get()[i], &bounds));
        ASSERT_TRUE(display.bounds.has_value());
        EXPECT_EQ(display.bounds->origin.x, bounds.x);
        EXPECT_EQ(display.bounds->origin.y, bounds.y);
        EXPECT_EQ(display.bounds->size.width, bounds.w);
        EXPECT_EQ(display.bounds->size.height, bounds.h);
        const auto* desktop = SDL_GetDesktopDisplayMode(ids.get()[i]);
        ASSERT_NE(desktop, nullptr);
        ASSERT_TRUE(display.desktopMode.has_value());
        ExpectMode(*display.desktopMode, *desktop);
        const auto* current = SDL_GetCurrentDisplayMode(ids.get()[i]);
        ASSERT_NE(current, nullptr);
        ASSERT_TRUE(display.currentMode.has_value());
        ExpectMode(*display.currentMode, *current);
        int modeCount = 0;
        const std::unique_ptr<SDL_DisplayMode*, decltype(&SDL_free)> modes(
            SDL_GetFullscreenDisplayModes(ids.get()[i], &modeCount), SDL_free);
        ASSERT_EQ(display.modes.size(), static_cast<std::size_t>(modeCount));
        for (int m = 0; m < modeCount; ++m) ExpectMode(display.modes[m], *modes.get()[m]);
    }
}

TEST_F(SDLDisplayCatalog, PrimaryProjectionRetainsLegacyOrderingAndOwnsItsData)
{
    const auto catalog = Platform::QueryDisplayCatalog();
    auto expected = Platform::ProjectPrimaryDisplayInfo(catalog, {800, 600});
    // Windows reorders only this copy; Linux's existing shim is a no-op.
    Platform::SDLCompatibility::OrderDisplayModes(expected);
    const auto actual = Platform::QueryDisplayInfo();
    ExpectModes(actual, expected);
    const auto legacy = GameDisplay::SelectResolutions(actual);
    const auto projected = GameDisplay::SelectResolutions(expected);
    ASSERT_EQ(legacy.count, projected.count);
    for (int i = 0; i < legacy.count; ++i) {
        EXPECT_EQ(legacy.modes[i].width, projected.modes[i].width);
        EXPECT_EQ(legacy.modes[i].height, projected.modes[i].height);
    }
    const auto raw = Platform::ProjectPrimaryDisplayInfo(catalog);
    Platform::ShutdownApplication();
    // Access every mode after SDL has released its video state (also under ASan).
    ExpectModes(Platform::ProjectPrimaryDisplayInfo(catalog), raw);
}
