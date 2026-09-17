#include "../Hunt/Platform/PlatformSDLInternal.h"
#include "../Hunt/Platform/LegacyKeyboardSDL.h"
#include <gtest/gtest.h>
TEST(SDLLinuxCompatibility, LegacyLettersNumbersAndOEMKeepTheirSavedSlots) {
    SDL_KeyboardEvent event{};
    auto key = [&](SDL_Scancode scan, SDL_Keycode code) {
        event.scancode=scan; event.key=code;
        return Platform::SDLCompatibility::LayoutKey(event, Platform::SDLInput::LegacyKey(scan,code,SDL_KMOD_NONE));
    };
    EXPECT_EQ(key(SDL_SCANCODE_Q,SDLK_A),'A');
    EXPECT_EQ(key(SDL_SCANCODE_A,'Q'),'Q');
    EXPECT_EQ(key(SDL_SCANCODE_1,'&'),'1');
    EXPECT_EQ(key(SDL_SCANCODE_0,0x00e0),'0');
    EXPECT_EQ(key(SDL_SCANCODE_A,0x0444),'A');
    EXPECT_EQ(key(SDL_SCANCODE_SEMICOLON,0x00f6),0xba);
    EXPECT_EQ(key(SDL_SCANCODE_NONUSBACKSLASH,'<'),0xe2);
    EXPECT_EQ(key(SDL_SCANCODE_UNKNOWN,0),0);
}
TEST(SDLLinuxCompatibility, SDLModeOrderRemainsDeterministic) {
    Platform::DisplayInfo info{{1920,1080}, {{{800,600},32},{{1024,768},32}}};
    Platform::SDLCompatibility::OrderDisplayModes(info);
    ASSERT_EQ(info.modes.size(),2u);
    EXPECT_EQ(info.modes[0].size.width,800);
}
