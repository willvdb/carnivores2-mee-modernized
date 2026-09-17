#pragma once

// Backend-private adapter. SDL types never enter Platform.h or saved bindings.
#include "Platform.h"
#include <SDL3/SDL_events.h>
#include <array>

namespace Platform::SDLInput {
std::uint8_t LegacyKey(SDL_Scancode scan, SDL_Keycode key, SDL_Keymod mods);
std::uint8_t MouseKey(std::uint8_t button);
KeyEvent TranslateKey(const SDL_KeyboardEvent& event, std::uint8_t legacyKey, bool altGrLayout = false);

// Advance only as each queued event is consumed. SDL's current keyboard state
// can already include later queued key/focus events after SDL_PumpEvents.
class Keyboard {
public:
    void Key(const SDL_KeyboardEvent& event, std::uint8_t legacyKey);
    void ClearDown();
    void Copy(KeyboardState& out, SDL_Keymod toggles, SDL_MouseButtonFlags mouse, bool altGrLayout = false) const;
private:
    std::array<std::uint8_t, SDL_SCANCODE_COUNT> held{};
};
} // namespace Platform::SDLInput
