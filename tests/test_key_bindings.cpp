#include <gtest/gtest.h>
#include "KeyBindings.h"
#include "../Hunt/Platform/PlatformWin32.h"

// Keep the native message fixtures as reference coverage for the decoder.
static bool Matches(int binding, unsigned int key, unsigned int data)
{
    return KeyDownMatches(binding, Platform::Win32::DecodeKeyEvent(key, data, false, false));
}
static bool Initial(unsigned int data)
{
    return !Platform::Win32::DecodeKeyEvent(0, data, false, false).repeat;
}


TEST(KeyBindings, SavedLeftShiftDefaultMatchesWindowsKeyMessage)
{
    EXPECT_TRUE(Matches(VK_LSHIFT, VK_SHIFT, 0x002A0001));
    EXPECT_FALSE(Matches(VK_LSHIFT, VK_SHIFT, 0x00360001));
    EXPECT_TRUE(Matches(VK_RSHIFT, VK_SHIFT, 0x00360001));
    EXPECT_FALSE(Matches(VK_RSHIFT, VK_SHIFT, 0x002A0001));
}

TEST(KeyBindings, ReboundGenericModifiersStillMatchEitherSide)
{
    EXPECT_TRUE(Matches(VK_SHIFT, VK_SHIFT, 0x002A0001));
    EXPECT_TRUE(Matches(VK_SHIFT, VK_SHIFT, 0x00360001));
    EXPECT_TRUE(Matches(VK_CONTROL, VK_CONTROL, 0x011D0001));
    EXPECT_TRUE(Matches(VK_MENU, VK_MENU, 0x01380001));
}

TEST(KeyBindings, ControlAndAltRespectExtendedSide)
{
    EXPECT_TRUE(Matches(VK_LCONTROL, VK_CONTROL, 0x001D0001));
    EXPECT_FALSE(Matches(VK_RCONTROL, VK_CONTROL, 0x001D0001));
    EXPECT_TRUE(Matches(VK_RCONTROL, VK_CONTROL, 0x011D0001));
    EXPECT_FALSE(Matches(VK_LCONTROL, VK_CONTROL, 0x011D0001));
    EXPECT_TRUE(Matches(VK_LMENU, VK_MENU, 0x00380001));
    EXPECT_FALSE(Matches(VK_RMENU, VK_MENU, 0x00380001));
    EXPECT_TRUE(Matches(VK_RMENU, VK_MENU, 0x01380001));
    EXPECT_FALSE(Matches(VK_LMENU, VK_MENU, 0x01380001));
}

TEST(KeyBindings, OrdinaryAndUnassignedBindings)
{
    EXPECT_TRUE(Matches('R', 'R', 1));
    EXPECT_FALSE(Matches('R', 'W', 1));
    EXPECT_FALSE(Matches(0, 0, 1));
    EXPECT_FALSE(Matches(-1, VK_SHIFT, 0x002A0001));
    EXPECT_FALSE(Matches(256, 'R', 1));
}

TEST(KeyBindings, HoldingToggleDoesNotRetrigger)
{
    EXPECT_TRUE(Initial(0x002A0001));
    EXPECT_FALSE(Initial(0x402A0001));
    EXPECT_TRUE(Initial(0x21380001)); // Alt context bit, first press
    EXPECT_FALSE(Initial(0x61380001));
}
