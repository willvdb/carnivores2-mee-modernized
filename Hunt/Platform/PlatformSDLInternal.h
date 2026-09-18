#pragma once
#include "Platform.h"
#include <SDL3/SDL.h>

namespace Platform::SDLDetails {
DisplayMode CopyDisplayMode(const SDL_DisplayMode& mode);
// Borrowed result: consume before freeing the SDL fullscreen-mode allocation.
const SDL_DisplayMode* FindNativeDisplayMode(SDL_DisplayMode* const* modes, int count,
                                           const DisplayMode& requested);
}

// Private backend bridge for genuinely deferred Windows compatibility.
namespace Platform::SDLCompatibility {
void SetProcessActive(bool active);
std::uint8_t LayoutKey(const SDL_KeyboardEvent& event, std::uint8_t fallback);
void OrderDisplayModes(DisplayInfo& info);
}
