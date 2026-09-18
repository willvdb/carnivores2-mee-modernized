#include "../Hunt/Platform/Platform.h"
#include "../Hunt/Platform/PlatformWin32.h"
#include <gtest/gtest.h>
#include <cstring>

TEST(PlatformWin32, PollsEveryLegacyKeyboardByteWithoutTranslation)
{
    // Thread-local keyboard state: no real key injection or visible window.
    Platform::KeyboardState saved{};
    ASSERT_TRUE(GetKeyboardState(saved));
    struct Restore {
        Platform::KeyboardState& state;
        ~Restore() { SetKeyboardState(state); }
    } restore{saved};

    Platform::KeyboardState supplied{}, native{}, actual{};
    for (int i = 0; i < 256; ++i)
        supplied[i] = static_cast<std::uint8_t>((i & 1 ? 0x80 : 0) | (i & 2 ? 1 : 0));
    ASSERT_TRUE(SetKeyboardState(supplied));
    ASSERT_TRUE(GetKeyboardState(native));
    ASSERT_TRUE(Platform::PollKeyboardState(actual));
    EXPECT_EQ(std::memcmp(actual, native, sizeof(actual)), 0);
    EXPECT_EQ(std::memcmp(actual, supplied, sizeof(actual)), 0);
}

TEST(PlatformWin32, PumpsOneMessageAndPreservesQuitCode)
{
    MSG message;
    while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {}
    int code = -7;
    EXPECT_EQ(Platform::PumpOneEvent(code), Platform::PumpResult::Idle);
    EXPECT_EQ(code, -7);
    ASSERT_TRUE(PostThreadMessage(GetCurrentThreadId(), WM_APP, 0, 0));
    ASSERT_TRUE(PostThreadMessage(GetCurrentThreadId(), WM_APP + 1, 0, 0));
    EXPECT_EQ(Platform::PumpOneEvent(code), Platform::PumpResult::Dispatched);
    EXPECT_EQ(Platform::PumpOneEvent(code), Platform::PumpResult::Dispatched);
    EXPECT_EQ(Platform::PumpOneEvent(code), Platform::PumpResult::Idle);
    EXPECT_EQ(code, -7);
    PostQuitMessage(23);
    EXPECT_EQ(Platform::PumpOneEvent(code), Platform::PumpResult::Quit);
    EXPECT_EQ(code, 23);
    Platform::RequestQuit();
    EXPECT_EQ(Platform::PumpOneEvent(code), Platform::PumpResult::Quit);
    EXPECT_EQ(code, 0);
}

TEST(PlatformWin32, CatalogPrimaryRetainsReferenceDimensionsDepthOrderAndIntegerRefresh)
{
    ASSERT_TRUE(Platform::InitializeApplication());
    const auto catalog = Platform::QueryDisplayCatalog();
    ASSERT_FALSE(catalog.displays.empty());
    ASSERT_TRUE(catalog.primaryDisplay.has_value());
    ASSERT_LT(*catalog.primaryDisplay, catalog.displays.size());
    const auto& primary = catalog.displays[*catalog.primaryDisplay];
    ASSERT_TRUE(primary.bounds.has_value());
    ASSERT_TRUE(primary.currentMode.has_value());
    ASSERT_TRUE(primary.desktopMode.has_value());
    DEVMODE native{};
    native.dmSize = sizeof(native);
    ASSERT_TRUE(EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &native));
    const auto expectMode = [](const Platform::DisplayMode& actual, const DEVMODE& mode) {
        EXPECT_EQ(actual.size.width, mode.dmPelsWidth);
        EXPECT_EQ(actual.size.height, mode.dmPelsHeight);
        EXPECT_EQ(actual.bitsPerPixel, mode.dmBitsPerPel);
        EXPECT_EQ(actual.refresh.numerator, mode.dmDisplayFrequency > 1 ? mode.dmDisplayFrequency : 0);
        EXPECT_EQ(actual.refresh.denominator, mode.dmDisplayFrequency > 1 ? 1u : 0u);
    };
    expectMode(*primary.currentMode, native);
    expectMode(*primary.desktopMode, native);
    const auto legacy = Platform::QueryDisplayInfo();
    const auto projected = Platform::ProjectPrimaryDisplayInfo(catalog);
    EXPECT_EQ(legacy.desktop.width, projected.desktop.width);
    EXPECT_EQ(legacy.desktop.height, projected.desktop.height);
    ASSERT_EQ(legacy.modes.size(), projected.modes.size());
    std::size_t count = 0;
    for (int i = 0; EnumDisplaySettings(nullptr, i, &native); ++i) {
        ASSERT_LT(count, primary.modes.size());
        expectMode(primary.modes[count], native);
        expectMode(legacy.modes[count], native);
        ++count;
    }
    EXPECT_EQ(count, primary.modes.size());
    for (const auto& display : catalog.displays) {
        ASSERT_TRUE(display.bounds.has_value());
        EXPECT_GT(display.bounds->size.width, 0);
        EXPECT_GT(display.bounds->size.height, 0);
    }
}
