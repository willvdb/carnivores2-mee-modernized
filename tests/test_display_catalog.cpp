// This translation unit deliberately has no backend include or link dependency.
#include "../Hunt/Platform/Platform.h"
#include "../Hunt/Game/DisplayModes.h"
#if defined(_WINDOWS_) || defined(SDL_h_) || defined(SDL_video_h_) || defined(_X11_XLIB_H_) || defined(WAYLAND_CLIENT_H)
#error Backend headers must not leak through the portable display API
#endif
#include <gtest/gtest.h>
#include <type_traits>

static_assert(std::is_same_v<decltype(Platform::Point::x), std::int32_t>);
static_assert(std::is_same_v<decltype(Platform::Point::y), std::int32_t>);

TEST(DisplayCatalog, RefreshPreservesWholeAndFractionalBackendValues)
{
    constexpr auto whole = Platform::MakeRefreshRate(60, 1);
    static_assert(whole.numerator == 60 && whole.denominator == 1);
    constexpr auto fractional = Platform::MakeRefreshRate(60000, 1001);
    static_assert(fractional.numerator == 60000 && fractional.denominator == 1001);
    // Preserve the backend's representation, including unreduced fractions.
    const auto unreduced = Platform::MakeRefreshRate(60000, 1000);
    EXPECT_EQ(unreduced.numerator, 60000u);
    EXPECT_EQ(unreduced.denominator, 1000u);
}

TEST(DisplayCatalog, UnspecifiedOrInvalidRefreshIsUnknown)
{
    for (const auto rate : {Platform::RefreshRate{}, Platform::MakeRefreshRate(0, 1),
                           Platform::MakeRefreshRate(60, 0), Platform::MakeRefreshRate(0, 0)}) {
        EXPECT_EQ(rate.numerator, 0u);
        EXPECT_EQ(rate.denominator, 0u);
    }
    const Platform::DisplayMode legacy{{800, 600}, 32};
    EXPECT_EQ(legacy.refresh.numerator, 0u);
    EXPECT_EQ(legacy.refresh.denominator, 0u);
}

TEST(DisplayCatalog, MultipleDisplaysKeepSignedBoundsAndSeparateCurrentDesktopModes)
{
    Platform::Display secondary;
    secondary.bounds = {{-1920, -240}, {1920, 1080}};
    secondary.desktopMode = {{1920, 1080}, 32, {60000, 1001}};
    secondary.currentMode = {{1280, 720}, 24, {60, 1}};
    secondary.modes = {*secondary.currentMode, *secondary.desktopMode};
    Platform::Display primary;
    primary.bounds = {{320, 120}, {2560, 1440}}; // No origin normalization.
    primary.desktopMode = {{2560, 1440}, 32, {144, 1}};
    primary.currentMode = primary.desktopMode;
    Platform::DisplayCatalog catalog{{secondary, primary}, 1};
    ASSERT_EQ(catalog.displays.size(), 2u);
    EXPECT_EQ(catalog.displays[0].bounds->origin.x, -1920);
    EXPECT_EQ(catalog.displays[0].bounds->origin.y, -240);
    EXPECT_EQ(catalog.displays[0].desktopMode->size.width, 1920);
    EXPECT_EQ(catalog.displays[0].currentMode->size.width, 1280);
    EXPECT_EQ(catalog.displays[0].currentMode->bitsPerPixel, 24u);
    EXPECT_EQ(catalog.displays[0].desktopMode->refresh.denominator, 1001u);
    const auto info = Platform::ProjectPrimaryDisplayInfo(catalog);
    EXPECT_EQ(info.desktop.width, 2560);
    EXPECT_EQ(info.desktop.height, 1440);
    EXPECT_EQ(catalog.displays[1].bounds->origin.x, 320);
    EXPECT_EQ(catalog.displays[1].bounds->origin.y, 120);
}

TEST(DisplayCatalog, PrimaryProjectionPreservesOrderDepthRefreshAndDimensionDuplicates)
{
    Platform::Display display;
    display.desktopMode = {{1920, 1080}, 32, {60, 1}};
    display.currentMode = {{800, 600}, 32, {60, 1}};
    display.modes = {{{1024, 768}, 32, {60000, 1001}}, {{800, 600}, 8, {}},
        {{2048, 768}, 32, {60, 1}}, {{1024, 1200}, 32, {60, 1}},
        {{640, 480}, 16, {}}, {{1024, 768}, 24, {120, 1}},
        {{1280, 720}, 32, {60, 1}}, {{640, 480}, 32, {75, 1}}};
    Platform::DisplayCatalog catalog{{{}, display}, 1};
    auto info = Platform::ProjectPrimaryDisplayInfo(catalog);
    ASSERT_EQ(info.modes.size(), display.modes.size());
    for (std::size_t i = 0; i < info.modes.size(); ++i) {
        EXPECT_EQ(info.modes[i].size.width, display.modes[i].size.width);
        EXPECT_EQ(info.modes[i].size.height, display.modes[i].size.height);
        EXPECT_EQ(info.modes[i].bitsPerPixel, display.modes[i].bitsPerPixel);
        EXPECT_EQ(info.modes[i].refresh.numerator, display.modes[i].refresh.numerator);
        EXPECT_EQ(info.modes[i].refresh.denominator, display.modes[i].refresh.denominator);
    }
    const auto resolutions = GameDisplay::SelectResolutions(info);
    ASSERT_EQ(resolutions.count, 4);
    EXPECT_EQ(resolutions.modes[0].width, 1024);
    EXPECT_EQ(resolutions.modes[1].width, 640);
    EXPECT_EQ(resolutions.modes[2].width, 1280);
    EXPECT_EQ(resolutions.modes[3].width, 1920);
    EXPECT_EQ(resolutions.modes[3].height, 1080);
    info.modes.clear(); // Projection owns its data.
    EXPECT_EQ(catalog.displays[1].modes.size(), 8u);
}

TEST(DisplayCatalog, UnknownPrimaryNeverFallsThroughToFirstDisplay)
{
    Platform::Display display;
    display.desktopMode = {{1920, 1080}, 32, {60, 1}};
    for (auto index : {std::optional<Platform::DisplayIndex>{},
                       std::optional<Platform::DisplayIndex>{1}}) {
        const auto info = Platform::ProjectPrimaryDisplayInfo({{display}, index});
        EXPECT_EQ(info.desktop.width, 0);
        EXPECT_EQ(info.desktop.height, 0);
        EXPECT_TRUE(info.modes.empty());
    }
    const auto empty = Platform::ProjectPrimaryDisplayInfo({}, {800, 600});
    EXPECT_EQ(empty.desktop.width, 800);
    EXPECT_EQ(empty.desktop.height, 600);
}

TEST(DisplayCatalog, MissingDesktopUsesBoundsThenExplicitCompatibilityFallback)
{
    Platform::Display display;
    display.currentMode = {{1024, 768}, 32, {60, 1}};
    // Current mode does not silently replace the desktop mode.
    auto info = Platform::ProjectDisplayInfo(display, {800, 600});
    EXPECT_EQ(info.desktop.width, 800);
    EXPECT_EQ(info.desktop.height, 600);
    display.bounds = {{-1920, -1080}, {1920, 1080}};
    info = Platform::ProjectDisplayInfo(display, {800, 600});
    EXPECT_EQ(info.desktop.width, 1920);
    EXPECT_EQ(info.desktop.height, 1080);
    EXPECT_FALSE(display.desktopMode.has_value());
}

TEST(DisplayCatalog, RawCatalogIsUncappedAndLegacyListDoesNotEvictForDesktop)
{
    Platform::Display display;
    display.desktopMode = {{1920, 1080}, 32, {60, 1}};
    for (int i = 0; i < 140; ++i) {
        display.modes.push_back({{800 + i, 600}, 32, {60, 1}});
        display.modes.push_back({{800 + i, 600}, 16, {75, 1}});
    }
    const auto info = Platform::ProjectPrimaryDisplayInfo({{display}, 0});
    ASSERT_EQ(info.modes.size(), 280u);
    const auto resolutions = GameDisplay::SelectResolutions(info);
    ASSERT_EQ(resolutions.count, 128);
    for (int i = 0; i < 128; ++i) EXPECT_EQ(resolutions.modes[i].width, 800 + i);
}
