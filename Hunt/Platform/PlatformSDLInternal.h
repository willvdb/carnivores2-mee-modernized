#pragma once
#include "Platform.h"
#include <SDL3/SDL.h>

// Private backend bridge for genuinely deferred Windows compatibility.
namespace Platform::SDLCompatibility {
void SetProcessActive(bool active);
std::uint8_t LayoutKey(const SDL_KeyboardEvent& event, std::uint8_t fallback);
void OrderDisplayModes(DisplayInfo& info);
}
