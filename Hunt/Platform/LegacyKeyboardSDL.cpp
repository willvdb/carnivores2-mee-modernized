#include "LegacyKeyboardSDL.h"
#include <SDL3/SDL_mouse.h>
#include <algorithm>

namespace Platform::SDLInput {
std::uint8_t LegacyKey(SDL_Scancode scan, SDL_Keycode key, SDL_Keymod mods)
{
    // VK letters follow the logical layout; never serialize SDL scancodes.
    if (key >= SDLK_A && key <= SDLK_Z) return static_cast<std::uint8_t>('A' + key - SDLK_A);
    if (key >= SDLK_0 && key <= SDLK_9) return static_cast<std::uint8_t>(key);
    if (scan >= SDL_SCANCODE_F1 && scan <= SDL_SCANCODE_F12)
        return static_cast<std::uint8_t>(0x70 + scan - SDL_SCANCODE_F1);
    if (scan >= SDL_SCANCODE_F13 && scan <= SDL_SCANCODE_F24)
        return static_cast<std::uint8_t>(0x7c + scan - SDL_SCANCODE_F13);
    if (scan >= SDL_SCANCODE_KP_1 && scan <= SDL_SCANCODE_KP_9) {
        constexpr std::uint8_t navigation[] = {0x23,0x28,0x22,0x25,0x0c,0x27,0x24,0x26,0x21};
        return (mods & SDL_KMOD_NUM) && !(mods & SDL_KMOD_SHIFT)
            ? static_cast<std::uint8_t>(0x61 + scan - SDL_SCANCODE_KP_1)
            : navigation[scan - SDL_SCANCODE_KP_1];
    }
    switch (scan) {
    case SDL_SCANCODE_BACKSPACE: return 0x08;
    case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_CLEAR: return 0x0c;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x0d;
    case SDL_SCANCODE_PAUSE: return 0x13;
    case SDL_SCANCODE_CAPSLOCK: return 0x14;
    case SDL_SCANCODE_ESCAPE: return 0x1b;
    case SDL_SCANCODE_SPACE: return 0x20;
    case SDL_SCANCODE_PAGEUP: return 0x21;
    case SDL_SCANCODE_PAGEDOWN: return 0x22;
    case SDL_SCANCODE_END: return 0x23;
    case SDL_SCANCODE_HOME: return 0x24;
    case SDL_SCANCODE_LEFT: return 0x25;
    case SDL_SCANCODE_UP: return 0x26;
    case SDL_SCANCODE_RIGHT: return 0x27;
    case SDL_SCANCODE_DOWN: return 0x28;
    case SDL_SCANCODE_SELECT: return 0x29;
    case SDL_SCANCODE_EXECUTE: return 0x2b;
    case SDL_SCANCODE_PRINTSCREEN: return 0x2c;
    case SDL_SCANCODE_INSERT: return 0x2d;
    case SDL_SCANCODE_DELETE: return 0x2e;
    case SDL_SCANCODE_HELP: return 0x2f;
    case SDL_SCANCODE_LGUI: return 0x5b;
    case SDL_SCANCODE_RGUI: return 0x5c;
    case SDL_SCANCODE_APPLICATION: return 0x5d;
    case SDL_SCANCODE_SLEEP: return 0x5f;
    case SDL_SCANCODE_KP_0: return (mods & SDL_KMOD_NUM) && !(mods & SDL_KMOD_SHIFT) ? 0x60 : 0x2d;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x6a;
    case SDL_SCANCODE_KP_PLUS: return 0x6b;
    case SDL_SCANCODE_KP_COMMA: return 0x6c;
    case SDL_SCANCODE_KP_MINUS: return 0x6d;
    case SDL_SCANCODE_KP_PERIOD: return (mods & SDL_KMOD_NUM) && !(mods & SDL_KMOD_SHIFT) ? 0x6e : 0x2e;
    case SDL_SCANCODE_KP_DIVIDE: return 0x6f;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x90;
    case SDL_SCANCODE_SCROLLLOCK: return 0x91;
    case SDL_SCANCODE_LSHIFT: return 0xa0;
    case SDL_SCANCODE_RSHIFT: return 0xa1;
    case SDL_SCANCODE_LCTRL: return 0xa2;
    case SDL_SCANCODE_RCTRL: return 0xa3;
    case SDL_SCANCODE_LALT: return 0xa4;
    case SDL_SCANCODE_RALT: return 0xa5;
    case SDL_SCANCODE_AC_BACK: return 0xa6;
    case SDL_SCANCODE_AC_FORWARD: return 0xa7;
    case SDL_SCANCODE_AC_REFRESH: return 0xa8;
    case SDL_SCANCODE_AC_STOP: return 0xa9;
    case SDL_SCANCODE_AC_SEARCH: return 0xaa;
    case SDL_SCANCODE_AC_BOOKMARKS: return 0xab;
    case SDL_SCANCODE_AC_HOME: return 0xac;
    case SDL_SCANCODE_MUTE: return 0xad;
    case SDL_SCANCODE_VOLUMEDOWN: return 0xae;
    case SDL_SCANCODE_VOLUMEUP: return 0xaf;
    case SDL_SCANCODE_MEDIA_NEXT_TRACK: return 0xb0;
    case SDL_SCANCODE_MEDIA_PREVIOUS_TRACK: return 0xb1;
    case SDL_SCANCODE_MEDIA_STOP: return 0xb2;
    case SDL_SCANCODE_MEDIA_PLAY_PAUSE: return 0xb3;
    // US fallback for OEM keys. Windows resolves native OEM/layout VKs in its
    // compatibility bridge, including layouts whose base glyph is non-ASCII.
    case SDL_SCANCODE_SEMICOLON: return 0xba;
    case SDL_SCANCODE_EQUALS: return 0xbb;
    case SDL_SCANCODE_COMMA: return 0xbc;
    case SDL_SCANCODE_MINUS: return 0xbd;
    case SDL_SCANCODE_PERIOD: return 0xbe;
    case SDL_SCANCODE_SLASH: return 0xbf;
    case SDL_SCANCODE_GRAVE: return 0xc0;
    case SDL_SCANCODE_LEFTBRACKET: return 0xdb;
    case SDL_SCANCODE_BACKSLASH: case SDL_SCANCODE_NONUSHASH: return 0xdc;
    case SDL_SCANCODE_RIGHTBRACKET: return 0xdd;
    case SDL_SCANCODE_APOSTROPHE: return 0xde;
    case SDL_SCANCODE_NONUSBACKSLASH: return 0xe2;
    default: return 0; // Unknown keys must not alias a valid saved binding.
    }
}

std::uint8_t MouseKey(std::uint8_t button)
{
    switch (button) {
    case SDL_BUTTON_LEFT: return 0x01;
    case SDL_BUTTON_RIGHT: return 0x02;
    case SDL_BUTTON_MIDDLE: return 0x04;
    case SDL_BUTTON_X1: return 0x05;
    case SDL_BUTTON_X2: return 0x06;
    default: return 0;
    }
}

KeyEvent TranslateKey(const SDL_KeyboardEvent& event, std::uint8_t legacyKey)
{
    std::uint8_t generic = legacyKey;
    if (legacyKey == 0xa0 || legacyKey == 0xa1) generic = 0x10;
    if (legacyKey == 0xa2 || legacyKey == 0xa3) generic = 0x11;
    if (legacyKey == 0xa4 || legacyKey == 0xa5) generic = 0x12;
    // Ctrl+Alt (including AltGr) is a normal WM_KEYDOWN; F10 is always a
    // system key. SDL has no separate SYSKEY event, so preserve that category.
    const bool system = generic == 0x79 ||
        ((event.mod & SDL_KMOD_ALT) && !(event.mod & SDL_KMOD_CTRL));
    return {generic, legacyKey, event.repeat, system, (event.mod & SDL_KMOD_SHIFT) != 0};
}

void Keyboard::Key(const SDL_KeyboardEvent& event, std::uint8_t legacyKey)
{
    if (event.scancode <= SDL_SCANCODE_UNKNOWN || event.scancode >= SDL_SCANCODE_COUNT) return;
    auto& key = held[event.scancode];
    // A modifier/layout change while held must not strand the old VK slot.
    if (!event.down) key = 0;
    else if (!event.repeat || !key) key = legacyKey;
}
void Keyboard::ClearDown() { held.fill(0); }

void Keyboard::Copy(KeyboardState& out, SDL_Keymod toggles, SDL_MouseButtonFlags mouse) const
{
    std::fill(std::begin(out), std::end(out), std::uint8_t{0});
    for (auto key : held) if (key) out[key] = 0x80;
    out[0x10] = out[0xa0] | out[0xa1];
    out[0x11] = out[0xa2] | out[0xa3];
    out[0x12] = out[0xa4] | out[0xa5];
    if (toggles & SDL_KMOD_CAPS) out[0x14] |= 1;
    if (toggles & SDL_KMOD_NUM) out[0x90] |= 1;
    if (toggles & SDL_KMOD_SCROLL) out[0x91] |= 1;
    for (std::uint8_t button = SDL_BUTTON_LEFT; button <= SDL_BUTTON_X2; ++button)
        if (mouse & SDL_BUTTON_MASK(button)) out[MouseKey(button)] = 0x80;
}
} // namespace Platform::SDLInput
