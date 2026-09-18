#include "../Hunt/Platform/PlatformSDLInternal.h"
#include "../Hunt/Game/DisplayModes.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <memory>

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
