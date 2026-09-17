#include "../Hunt/Platform/PlatformWin32.h"
#include "../Hunt/Platform/PlatformSDLInternal.h"
#include "../Hunt/Game/DisplayModes.h"
#include <gtest/gtest.h>

TEST(SDLWindowsCompatibility, FrenchLayoutKeepsLegacyLetterNumberAndOEMBindings)
{
    const HKL previous = GetKeyboardLayout(0);
    const HKL french = LoadKeyboardLayoutW(L"0000040c", KLF_NOTELLSHELL);
    ASSERT_NE(french, nullptr);
    ASSERT_NE(ActivateKeyboardLayout(french, 0), nullptr);
    struct Restore {
        HKL previous, loaded;
        ~Restore() { Platform::ShutdownApplication(); ActivateKeyboardLayout(previous, 0); if (loaded != previous) UnloadKeyboardLayout(loaded); }
    } restore{previous, french};
    SDL_KeyboardEvent key{};
    key.raw=0x10; // QWERTY Q position is French A.
    EXPECT_EQ(Platform::SDLCompatibility::LayoutKey(key, 'Q'),'A');
    key.raw=0x1e;
    EXPECT_EQ(Platform::SDLCompatibility::LayoutKey(key, 'A'),'Q');
    key.raw=0x02; // Unshifted '&' is still VK_1.
    EXPECT_EQ(Platform::SDLCompatibility::LayoutKey(key, 0),'1');
    key.raw=0x0d; // French '=' key is VK_OEM_PLUS.
    EXPECT_EQ(Platform::SDLCompatibility::LayoutKey(key, 0),VK_OEM_PLUS);
    key.raw=0; // Synthetic SDL event without a native scan uses portable adapter.
    EXPECT_EQ(Platform::SDLCompatibility::LayoutKey(key, 'W'),'W');

    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"windows"));
    ASSERT_TRUE(Platform::InitializeApplication()) << Platform::LastError();
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    SDL_Event native{}; native.type=SDL_EVENT_KEY_DOWN;
    native.key.scancode=SDL_SCANCODE_RALT; native.key.key=SDLK_RALT;
    native.key.mod=SDL_KMOD_RALT; native.key.down=true;
    ASSERT_TRUE(SDL_PushEvent(&native));
    Platform::Event event; int code=-1;
    ASSERT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Dispatched);
    EXPECT_EQ(event.key.key,VK_CONTROL); EXPECT_EQ(event.key.sidedKey,VK_LCONTROL);
    ASSERT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Dispatched);
    EXPECT_EQ(event.key.key,VK_MENU); EXPECT_EQ(event.key.sidedKey,VK_RMENU);
    EXPECT_FALSE(event.key.system);
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Idle);
    Platform::KeyboardState state{}; ASSERT_TRUE(Platform::PollKeyboardState(state));
    EXPECT_EQ(state[VK_LCONTROL],0x80); EXPECT_EQ(state[VK_CONTROL],0x80);
    EXPECT_EQ(state[VK_RMENU],0x80); EXPECT_EQ(state[VK_MENU],0x80);
}
TEST(SDLWindowsCompatibility, SDLResolutionOrdinalsMatchUntouchedWindowsMenu)
{
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"windows"));
    ASSERT_TRUE(Platform::InitializeApplication()) << Platform::LastError();
    struct Quit { ~Quit() { Platform::ShutdownApplication(); } } quit;
    Platform::DisplayInfo native{};
    DEVMODE mode{}; mode.dmSize=sizeof(mode);
    ASSERT_TRUE(EnumDisplaySettings(nullptr,ENUM_CURRENT_SETTINGS,&mode));
    native.desktop={static_cast<std::int32_t>(mode.dmPelsWidth),static_cast<std::int32_t>(mode.dmPelsHeight)};
    for (int i=0;EnumDisplaySettings(nullptr,i,&mode);++i)
        native.modes.push_back({{static_cast<std::int32_t>(mode.dmPelsWidth),static_cast<std::int32_t>(mode.dmPelsHeight)},mode.dmBitsPerPel});
    const auto expected=GameDisplay::SelectResolutions(native);
    const auto actual=GameDisplay::SelectResolutions(Platform::QueryDisplayInfo());
    ASSERT_EQ(actual.count,expected.count);
    for (int i=0;i<actual.count;++i) {
        EXPECT_EQ(actual.modes[i].width,expected.modes[i].width) << i;
        EXPECT_EQ(actual.modes[i].height,expected.modes[i].height) << i;
    }
}

TEST(SDLWindowsCompatibility, OrdinaryRightAltDoesNotBecomeAltGrControl)
{
    const HKL previous=GetKeyboardLayout(0);
    const HKL english=LoadKeyboardLayoutW(L"00000409",KLF_NOTELLSHELL);
    ASSERT_NE(english,nullptr);
    ASSERT_NE(ActivateKeyboardLayout(english,0),nullptr);
    struct Restore {
        HKL previous,loaded;
        ~Restore() { Platform::ShutdownApplication(); ActivateKeyboardLayout(previous,0); if(loaded!=previous)UnloadKeyboardLayout(loaded); }
    } restore{previous,english};
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"windows"));
    ASSERT_TRUE(Platform::InitializeApplication()) << Platform::LastError();
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    SDL_Event native{}; native.type=SDL_EVENT_KEY_DOWN;
    native.key.scancode=SDL_SCANCODE_RALT; native.key.key=SDLK_RALT;
    native.key.mod=SDL_KMOD_RALT; native.key.down=true;
    ASSERT_TRUE(SDL_PushEvent(&native));
    Platform::Event event; int code=-1;
    ASSERT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Dispatched);
    EXPECT_EQ(event.key.key,VK_MENU); EXPECT_EQ(event.key.sidedKey,VK_RMENU);
    EXPECT_TRUE(event.key.system);
    Platform::KeyboardState state{}; ASSERT_TRUE(Platform::PollKeyboardState(state));
    EXPECT_EQ(state[VK_RMENU],0x80); EXPECT_EQ(state[VK_LCONTROL],0);
}
