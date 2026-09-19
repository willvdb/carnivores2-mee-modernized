#pragma once
#include "Platform.h"
#include <SDL3/SDL.h>

namespace Platform::SDLDetails {
using MouseStateQuery = SDL_MouseButtonFlags (SDLCALL *)(float*, float*);
// No absolute-coordinate fallback when Wayland relative capture is unavailable.
MouseDelta ReadWaylandMouseLook(bool captured, bool focused,
                               MouseStateQuery query = SDL_GetRelativeMouseState);

struct NativeDisplay { SDL_DisplayID id; DisplayBounds bounds; };
struct WindowDisplay {
    SDL_DisplayID id;
    std::optional<DisplayTarget> target;
    std::optional<DisplayMode> exclusiveMode;
    bool ambiguousBounds = false;
};
// Mechanism only: exactly one full-rectangle match, no catalog index or eligibility.
WindowDisplay MapWindowDisplay(const std::vector<NativeDisplay>& displays, SDL_DisplayID primary,
                               std::optional<DisplayTarget> target, std::optional<DisplayMode> mode);
DisplayMode CopyDisplayMode(const SDL_DisplayMode& mode);
// Borrowed result: consume before freeing the SDL fullscreen-mode allocation.
const SDL_DisplayMode* FindNativeDisplayMode(SDL_DisplayMode* const* modes, int count,
                                           const DisplayMode& requested, SDL_DisplayID display);
using ClosestModeQuery = bool (SDLCALL *)(SDL_DisplayID, int, int, float, bool, SDL_DisplayMode*);
// SDL's automatic rate (0) is retained; a close size is never applied.
bool FindAutomaticDisplayMode(SDL_DisplayID display, Size size, SDL_DisplayMode& result,
                             ClosestModeQuery query = SDL_GetClosestFullscreenDisplayMode);
}

// Private backend bridge for genuinely deferred Windows compatibility.
namespace Platform::SDLCompatibility {
void SetProcessActive(bool active);
std::uint8_t LayoutKey(const SDL_KeyboardEvent& event, std::uint8_t fallback);
void OrderDisplayModes(DisplayInfo& info);
void DiscoverMonitorIdentities(DisplayCatalog& catalog);
}
