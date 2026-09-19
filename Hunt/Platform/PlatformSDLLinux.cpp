#include "PlatformSDLInternal.h"
#include "LegacyKeyboardSDL.h"
#include "DisplayIdentityLinuxNative.h"

namespace Platform::SDLCompatibility {
void DiscoverMonitorIdentities(DisplayCatalog& catalog, const SDL_DisplayID* ids, int count)
{
    for (auto& display : catalog.displays) display.identityStatus = "backend has no supported native identity contract";
    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver || !ids || count < 0 || static_cast<std::size_t>(count) != catalog.displays.size()) return;
    if (SDL_strcmp(driver, "x11") == 0) LinuxIdentity::DiscoverX11(catalog, ids, count);
    if (SDL_strcmp(driver, "wayland") == 0) LinuxIdentity::DiscoverWayland(catalog, ids, count);
}
SDL_DisplayID PrimaryDisplay()
{
    const char* driver=SDL_GetCurrentVideoDriver();
    if (driver && SDL_strcmp(driver,"x11")==0)
        if (const auto primary=LinuxIdentity::X11PrimaryDisplay()) return primary;
    return SDL_GetPrimaryDisplay();
}
void ShutdownMonitorDiscovery() { LinuxIdentity::ShutdownWaylandDiscovery(); }
void SetProcessActive(bool) {} // No privileged process priority changes on Linux.
void OrderDisplayModes(DisplayInfo&) {} // Linux has no Win32 driver-order ordinal.
std::uint8_t LayoutKey(const SDL_KeyboardEvent& event, std::uint8_t fallback)
{
    // Windows number-row VKs identify digits even on AZERTY layouts whose
    // unshifted glyphs are punctuation. Preserve this saved meaning.
    if (event.scancode >= SDL_SCANCODE_1 && event.scancode <= SDL_SCANCODE_9)
        return static_cast<std::uint8_t>('1' + event.scancode - SDL_SCANCODE_1);
    if (event.scancode == SDL_SCANCODE_0) return '0';
    // Event keycodes already use SDL's layout; normalize shifted ASCII letters.
    if (event.key >= 'A' && event.key <= 'Z') return static_cast<std::uint8_t>(event.key);
    if (event.key >= SDLK_A && event.key <= SDLK_Z) return static_cast<std::uint8_t>('A' + event.key - SDLK_A);
    // Non-Latin layouts retain the physical US letter VK. OEM fallback stays
    // the shared US physical mapping; Windows-specific IME/OEM layouts are not
    // represented by SDL keycodes and are documented as a compatibility limit.
    if (event.scancode >= SDL_SCANCODE_A && event.scancode <= SDL_SCANCODE_Z)
        return static_cast<std::uint8_t>('A' + event.scancode - SDL_SCANCODE_A);
    return fallback;
}
}
