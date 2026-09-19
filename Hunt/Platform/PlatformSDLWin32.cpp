#include "PlatformWin32.h"
#include "PlatformSDLInternal.h"
#include "DisplayIdentityWin32.h"
#include <algorithm>

namespace Platform::Win32 {
HWND GameWindow()
{
    // The SDL window remains authoritative; borrowed only by legacy GDI/audio.
    extern SDL_Window* SDLGameWindow();
    auto* window = SDLGameWindow();
    return window ? static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) : nullptr;
}
}

namespace Platform::SDLCompatibility {
SDL_DisplayID PrimaryDisplay() { return SDL_GetPrimaryDisplay(); }
void DiscoverMonitorIdentities(DisplayCatalog& catalog, const SDL_DisplayID*, int)
{
    const auto* driver = SDL_GetCurrentVideoDriver();
    if (driver && SDL_strcmp(driver, "windows") == 0) Win32Details::DiscoverMonitorIdentities(catalog);
}
void ShutdownMonitorDiscovery() {}
void SetProcessActive(bool active)
{
    // SDL has thread priority, not the legacy process-wide priority operation.
    SetPriorityClass(GetCurrentProcess(), active ? HIGH_PRIORITY_CLASS : IDLE_PRIORITY_CLASS);
}

std::uint8_t LayoutKey(const SDL_KeyboardEvent& event, std::uint8_t fallback)
{
    // SDL supplies the native scancode. Resolve layout-dependent VK identity
    // only (letters, number row, OEM/IME); SDL still owns all input/events.
    // Character-to-VK lookup would lose dead keys and non-Latin layouts.
    if (!event.raw) return fallback;
    const auto vk = MapVirtualKeyExW(event.raw, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') ||
        (vk >= 0xba && vk <= 0xe2) || (vk >= 0x15 && vk <= 0x1f))
        return static_cast<std::uint8_t>(vk);
    return fallback;
}

void OrderDisplayModes(DisplayInfo& info)
{
    // SDL sorts modes; old profiles AND the untouched menu store a driver-order
    // resolution index. Keep that ordinal while SDL supplies the actual modes.
    std::vector<DisplayMode> ordered;
    DEVMODE mode{};
    mode.dmSize = sizeof(mode);
    for (int i = 0; EnumDisplaySettings(nullptr, i, &mode); ++i) {
        if (mode.dmBitsPerPel < 16) continue;
        const auto found = std::find_if(info.modes.begin(), info.modes.end(), [&](const DisplayMode& candidate) {
            return candidate.size.width == static_cast<int>(mode.dmPelsWidth) &&
                   candidate.size.height == static_cast<int>(mode.dmPelsHeight);
        });
        if (found != info.modes.end()) ordered.push_back(*found);
    }
    // Do not discard SDL-only modes; portable filtering/dedup/cap still happens
    // in GameDisplay::SelectResolutions, just as for the Win32 reference.
    ordered.insert(ordered.end(), info.modes.begin(), info.modes.end());
    info.modes = std::move(ordered);
}
}
