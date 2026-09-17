#include "../Hunt/Platform/Platform.h"
#include "../Hunt/Core/KeyBindings.h"
#ifdef _WINDOWS_
#error Portable key matching must not include Windows headers
#endif
#include "../Hunt/Platform/LegacyKeyboardSDL.h"
#include <gtest/gtest.h>

using namespace Platform::SDLInput;
namespace {
SDL_KeyboardEvent Key(SDL_Scancode scan, SDL_Keycode code, SDL_Keymod mods = SDL_KMOD_NONE,
                      bool down = true, bool repeat = false)
{
    SDL_KeyboardEvent e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.scancode = scan; e.key = code; e.mod = mods; e.down = down; e.repeat = repeat;
    return e;
}
void Apply(Keyboard& keyboard, const SDL_KeyboardEvent& e)
{
    keyboard.Key(e, LegacyKey(e.scancode, e.key, e.mod));
}
}

TEST(LegacySDL, LettersUseLogicalLayoutAndNumbersKeepVKIndices)
{
    for (int i=0; i<26; ++i)
        EXPECT_EQ(LegacyKey(static_cast<SDL_Scancode>(SDL_SCANCODE_A+i), SDLK_A+i, 0), 'A'+i);
    // A French physical A/Q swap must not reinterpret stored logical bindings.
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_A, SDLK_Q, 0), 'Q');
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_Q, SDLK_A, 0), 'A');
    for (int i=0; i<10; ++i) EXPECT_EQ(LegacyKey(SDL_SCANCODE_UNKNOWN, SDLK_0+i, 0), '0'+i);
}
TEST(LegacySDL, NavigationFunctionAndOEMKeys)
{
    const struct { SDL_Scancode scan; int vk; } cases[] = {
        {SDL_SCANCODE_LEFT,0x25},{SDL_SCANCODE_UP,0x26},{SDL_SCANCODE_RIGHT,0x27},{SDL_SCANCODE_DOWN,0x28},
        {SDL_SCANCODE_HOME,0x24},{SDL_SCANCODE_END,0x23},{SDL_SCANCODE_PAGEUP,0x21},{SDL_SCANCODE_PAGEDOWN,0x22},
        {SDL_SCANCODE_INSERT,0x2d},{SDL_SCANCODE_DELETE,0x2e},{SDL_SCANCODE_PAUSE,0x13},{SDL_SCANCODE_RETURN,0x0d},
        {SDL_SCANCODE_ESCAPE,0x1b},{SDL_SCANCODE_TAB,0x09},{SDL_SCANCODE_LEFTBRACKET,0xdb},
        {SDL_SCANCODE_RIGHTBRACKET,0xdd},{SDL_SCANCODE_NONUSBACKSLASH,0xe2}};
    for (const auto& c : cases) EXPECT_EQ(LegacyKey(c.scan, 0, 0), c.vk);
    for (int i=0;i<12;++i) {
        EXPECT_EQ(LegacyKey(static_cast<SDL_Scancode>(SDL_SCANCODE_F1+i),0,0),0x70+i);
        EXPECT_EQ(LegacyKey(static_cast<SDL_Scancode>(SDL_SCANCODE_F13+i),0,0),0x7c+i);
    }
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_UNKNOWN, 0, 0), 0);
}
TEST(LegacySDL, KeypadPreservesNumLockAndNavigationMeanings)
{
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_KP_1, SDLK_KP_1, SDL_KMOD_NUM), 0x61);
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_KP_1, SDLK_KP_1, 0), 0x23);
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_KP_1, SDLK_KP_1, SDL_KMOD_NUM|SDL_KMOD_SHIFT), 0x23);
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_KP_0, SDLK_KP_0, SDL_KMOD_NUM), 0x60);
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_KP_0, SDLK_KP_0, 0), 0x2d);
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_KP_PERIOD, SDLK_KP_PERIOD, SDL_KMOD_NUM), 0x6e);
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_KP_PERIOD, SDLK_KP_PERIOD, 0), 0x2e);
    EXPECT_EQ(LegacyKey(SDL_SCANCODE_KP_ENTER, SDLK_KP_ENTER, 0), 0x0d);
}
TEST(LegacySDL, SidedModifiersMatchGenericAndOnlyTheirOwnSide)
{
    const SDL_Scancode scans[] = {SDL_SCANCODE_LSHIFT,SDL_SCANCODE_RSHIFT,SDL_SCANCODE_LCTRL,
        SDL_SCANCODE_RCTRL,SDL_SCANCODE_LALT,SDL_SCANCODE_RALT};
    for (int i=0;i<6;++i) {
        const auto e = Key(scans[i], 0);
        const auto key = LegacyKey(e.scancode,e.key,e.mod);
        EXPECT_EQ(key,0xa0+i);
        const auto translated = TranslateKey(e,key);
        EXPECT_TRUE(KeyDownMatches(0x10+i/2,translated));
        EXPECT_TRUE(KeyDownMatches(0xa0+i,translated));
        EXPECT_FALSE(KeyDownMatches(0xa0+(i^1),translated));
        Keyboard keyboard; Apply(keyboard,e);
        Platform::KeyboardState state{}; keyboard.Copy(state,0,0);
        EXPECT_EQ(state[0xa0+i],0x80);
        EXPECT_EQ(state[0x10+i/2],0x80);
        EXPECT_EQ(state[0xa0+(i^1)],0);
    }
}
TEST(LegacySDL, ReleasingOneShiftKeepsOtherShiftAndGenericDown)
{
    Keyboard keyboard;
    Apply(keyboard,Key(SDL_SCANCODE_LSHIFT,0)); Apply(keyboard,Key(SDL_SCANCODE_RSHIFT,0));
    Apply(keyboard,Key(SDL_SCANCODE_LSHIFT,0,0,false));
    Platform::KeyboardState state{}; keyboard.Copy(state,0,0);
    EXPECT_EQ(state[0x10],0x80); EXPECT_EQ(state[0xa0],0); EXPECT_EQ(state[0xa1],0x80);
}
TEST(LegacySDL, MouseSlotsDoNotFollowSDLButtonOrder)
{
    EXPECT_EQ(MouseKey(SDL_BUTTON_LEFT),0x01); EXPECT_EQ(MouseKey(SDL_BUTTON_RIGHT),0x02);
    EXPECT_EQ(MouseKey(SDL_BUTTON_MIDDLE),0x04); EXPECT_EQ(MouseKey(SDL_BUTTON_X1),0x05);
    EXPECT_EQ(MouseKey(SDL_BUTTON_X2),0x06); EXPECT_EQ(MouseKey(0),0);
    Keyboard keyboard;
    for (std::uint8_t b=1;b<=5;++b) {
        Platform::KeyboardState state{}; keyboard.Copy(state,0,SDL_BUTTON_MASK(b));
        for (int i=0;i<256;++i) EXPECT_EQ(state[i],i==MouseKey(b)?0x80:0);
    }
}
TEST(LegacySDL, ToggleBitsCoexistWithDownAndSurviveFocusLoss)
{
    Keyboard keyboard;
    Apply(keyboard,Key(SDL_SCANCODE_CAPSLOCK,0)); Apply(keyboard,Key(SDL_SCANCODE_NUMLOCKCLEAR,0));
    Apply(keyboard,Key(SDL_SCANCODE_SCROLLLOCK,0));
    constexpr SDL_Keymod toggles = SDL_KMOD_CAPS | SDL_KMOD_NUM | SDL_KMOD_SCROLL;
    Platform::KeyboardState state{}; keyboard.Copy(state,toggles,0);
    for (int vk : {0x14,0x90,0x91}) EXPECT_EQ(state[vk],0x81);
    keyboard.ClearDown(); keyboard.Copy(state,toggles,0);
    for (int vk : {0x14,0x90,0x91}) EXPECT_EQ(state[vk],1);
    keyboard.Copy(state,0,0);
    for (auto value : state) EXPECT_EQ(value,0);
}
TEST(LegacySDL, ReleaseAndRepeatCannotStrandOldLayoutOrKeypadBinding)
{
    Keyboard keyboard;
    Apply(keyboard,Key(SDL_SCANCODE_A,SDLK_A));
    Apply(keyboard,Key(SDL_SCANCODE_A,SDLK_Q,0,true,true));
    Platform::KeyboardState state{}; keyboard.Copy(state,0,0);
    EXPECT_EQ(state['A'],0x80); EXPECT_EQ(state['Q'],0);
    Apply(keyboard,Key(SDL_SCANCODE_A,SDLK_Q,0,false));
    keyboard.Copy(state,0,0); EXPECT_EQ(state['A'],0); EXPECT_EQ(state['Q'],0);
    Apply(keyboard,Key(SDL_SCANCODE_KP_1,SDLK_KP_1,SDL_KMOD_NUM));
    Apply(keyboard,Key(SDL_SCANCODE_KP_1,SDLK_KP_1,0,false));
    keyboard.Copy(state,0,0); EXPECT_EQ(state[0x61],0); EXPECT_EQ(state[0x23],0);
}
TEST(LegacySDL, SystemKeysShiftAndRepeatRemainEngineDecisions)
{
    auto e=Key(SDL_SCANCODE_RETURN,SDLK_RETURN,SDL_KMOD_LALT);
    EXPECT_TRUE(TranslateKey(e,0x0d).system);
    e.mod=SDL_KMOD_LALT|SDL_KMOD_LCTRL; EXPECT_FALSE(TranslateKey(e,0x0d).system);
    e=Key(SDL_SCANCODE_F10,SDLK_F10); EXPECT_TRUE(TranslateKey(e,0x79).system);
    e=Key(SDL_SCANCODE_A,SDLK_A,SDL_KMOD_RSHIFT,true,true);
    auto translated=TranslateKey(e,'A'); EXPECT_TRUE(translated.shift); EXPECT_TRUE(translated.repeat);
    EXPECT_FALSE(KeyDownMatches(0,translated)); EXPECT_FALSE(KeyDownMatches(256,translated));
    EXPECT_FALSE(KeyDownMatches(-1,translated));
}
TEST(LegacySDL, UnknownScancodesCannotWriteOutsideLegacyBuffer)
{
    Keyboard keyboard;
    Apply(keyboard,Key(SDL_SCANCODE_UNKNOWN,0));
    Apply(keyboard,Key(static_cast<SDL_Scancode>(-1),SDLK_A));
    Apply(keyboard,Key(SDL_SCANCODE_COUNT,SDLK_A));
    Platform::KeyboardState state{}; keyboard.Copy(state,0,0);
    for(auto value:state) EXPECT_EQ(value,0);
}

TEST(LegacySDL, AltGrRestoresSyntheticLeftControlWithoutConfusingRightControl)
{
    Keyboard keyboard;
    const auto e=Key(SDL_SCANCODE_RALT,SDLK_RALT,SDL_KMOD_RALT);
    Apply(keyboard,e);
    Platform::KeyboardState state{}; keyboard.Copy(state,0,0,true);
    EXPECT_EQ(state[0xa5],0x80); EXPECT_EQ(state[0x12],0x80);
    EXPECT_EQ(state[0xa2],0x80); EXPECT_EQ(state[0x11],0x80); EXPECT_EQ(state[0xa3],0);
    EXPECT_FALSE(TranslateKey(e,0xa5,true).system);
    EXPECT_TRUE(TranslateKey(e,0xa5,false).system); // Ordinary right Alt on US layout.
    Apply(keyboard,Key(SDL_SCANCODE_RALT,SDLK_RALT,0,false));
    keyboard.Copy(state,0,0,true); EXPECT_EQ(state[0xa2],0); EXPECT_EQ(state[0x11],0);
}
