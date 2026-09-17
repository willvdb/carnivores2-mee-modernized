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
